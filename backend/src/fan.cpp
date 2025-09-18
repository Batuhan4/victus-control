#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/un.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <optional>
#include <dirent.h>
#include <cctype>
#include <array>
#include <vector>
#include <cstring>
#include <algorithm>
#include <exception>
#include <cmath>

#include "fan.hpp"
#include "util.hpp"
#include "config.hpp"
#include "logger.hpp"
#include "validation.hpp"

static std::atomic<int> fan_thread_generation(0);
static std::atomic<bool> is_reapplying(false);
static std::mutex fan_state_mutex;
static std::optional<std::string> last_fan1_speed;
static std::optional<std::string> last_fan2_speed;
static std::mutex mode_mutex;
static std::string requested_mode = "AUTO";

static std::atomic<bool> better_auto_running(false);
static std::thread better_auto_thread;
static std::chrono::steady_clock::time_point better_auto_last_manual_assert;

static std::once_flag cpu_sensor_once;
static std::once_flag gpu_sensor_once;
static std::once_flag gpu_usage_once;
static std::optional<std::string> cpu_temp_path;
static std::optional<std::string> gpu_temp_path;
static std::optional<std::string> gpu_busy_path;
static std::atomic<bool> cpu_sensor_warned(false);
static std::atomic<bool> gpu_sensor_warned(false);
static std::atomic<bool> gpu_usage_warned(false);

// Global shutdown flag for clean thread termination
static std::atomic<bool> global_shutdown(false);

struct CpuSampleTimes {
    unsigned long long idle;
    unsigned long long total;
};
static std::mutex cpu_usage_mutex;
static std::optional<CpuSampleTimes> previous_cpu_times;

// Constants moved to config.hpp

static std::array<std::once_flag, 2> fan_max_once;
static std::array<int, 2> fan_max_cache = Config::BETTER_AUTO_MAX_FALLBACK;
static std::mutex fan_apply_mutex;
static std::array<std::chrono::steady_clock::time_point, 2> fan_last_apply = {
    std::chrono::steady_clock::time_point::min(),
    std::chrono::steady_clock::time_point::min()
};

struct ThermalSnapshot {
    std::optional<double> cpu_temp_c;
    std::optional<double> gpu_temp_c;
    std::optional<double> cpu_usage_pct;
    std::optional<double> gpu_usage_pct;
};

static std::string to_lower_copy(const std::string &input)
{
    std::string lowered = input;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lowered;
}

static std::optional<std::string> find_thermal_zone_by_type(const std::vector<std::string> &hints)
{
    DIR *dir = opendir("/sys/class/thermal");
    if (!dir) {
        return std::nullopt;
    }

    std::optional<std::string> fallback;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        if (std::strncmp(entry->d_name, "thermal_zone", 12) != 0) {
            continue;
        }

        std::string base_path = std::string("/sys/class/thermal/") + entry->d_name;
        std::ifstream type_file(base_path + "/type");
        if (!type_file) {
            continue;
        }

        std::string sensor_type;
        std::getline(type_file, sensor_type);
        std::string lowered = to_lower_copy(sensor_type);

        if (!fallback) {
            fallback = base_path + "/temp";
        }

        for (const auto &hint : hints) {
            if (lowered.find(hint) != std::string::npos) {
                closedir(dir);
                return base_path + "/temp";
            }
        }
    }

    closedir(dir);
    return fallback;
}

static std::optional<std::string> find_hwmon_temp_sensor(const std::vector<std::string> &name_hints,
                                                         const std::vector<std::string> &label_hints)
{
    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir) {
        return std::nullopt;
    }

    std::optional<std::string> fallback;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        if (std::strncmp(entry->d_name, "hwmon", 5) != 0) {
            continue;
        }

        std::string base_path = std::string("/sys/class/hwmon/") + entry->d_name;
        std::string name_path = base_path + "/name";
        std::ifstream name_file(name_path);
        std::string name_value;
        if (name_file) {
            std::getline(name_file, name_value);
        }
        std::string lowered_name = to_lower_copy(name_value);

        bool name_matches = false;
        for (const auto &hint : name_hints) {
            if (!hint.empty() && lowered_name.find(hint) != std::string::npos) {
                name_matches = true;
                break;
            }
        }

        DIR *inner = opendir(base_path.c_str());
        if (!inner) {
            continue;
        }

        std::vector<std::string> input_candidates;
        struct dirent *inner_entry;
        while ((inner_entry = readdir(inner)) != nullptr)
        {
            std::string file_name = inner_entry->d_name;
            if (file_name.rfind("temp", 0) != 0) {
                continue;
            }
            if (file_name.find("_input") == std::string::npos) {
                continue;
            }

            std::string input_path = base_path + "/" + file_name;
            input_candidates.push_back(input_path);

            std::string prefix = file_name.substr(0, file_name.find("_input"));
            std::string label_path = base_path + "/" + prefix + "_label";

            std::ifstream label_file(label_path);
            if (label_file) {
                std::string label_value;
                std::getline(label_file, label_value);
                std::string lowered_label = to_lower_copy(label_value);

                for (const auto &hint : label_hints) {
                    if (!hint.empty() && lowered_label.find(hint) != std::string::npos) {
                        closedir(inner);
                        closedir(dir);
                        return input_path;
                    }
                }
            }
        }
        closedir(inner);

        if (name_matches && !input_candidates.empty()) {
            closedir(dir);
            return input_candidates.front();
        }

        if (!fallback && !input_candidates.empty()) {
            fallback = input_candidates.front();
        }
    }

    closedir(dir);
    return fallback;
}

static std::optional<std::string> locate_cpu_temp_sensor()
{
    std::call_once(cpu_sensor_once, []() {
        const std::vector<std::string> hwmon_name_hints = {"k10temp", "coretemp", "zenpower", "cpu", "package", "soc"};
        const std::vector<std::string> hwmon_label_hints = {"cpu", "package", "soc"};
        cpu_temp_path = find_hwmon_temp_sensor(hwmon_name_hints, hwmon_label_hints);

        if (!cpu_temp_path) {
            const std::vector<std::string> zone_hints = {"x86_pkg", "tctl", "cpu", "soc"};
            cpu_temp_path = find_thermal_zone_by_type(zone_hints);
        }

        if (!cpu_temp_path && !cpu_sensor_warned.exchange(true)) {
            std::cerr << "better-auto: CPU thermal sensor not found; automatic mode will use default fan steps" << std::endl;
        }
    });
    return cpu_temp_path;
}

static std::optional<std::string> locate_gpu_temp_sensor()
{
    std::call_once(gpu_sensor_once, []() {
        const std::vector<std::string> hwmon_name_hints = {"amdgpu", "radeon", "nvidia", "gpu"};
        const std::vector<std::string> hwmon_label_hints = {"edge", "gpu", "junction", "hotspot"};
        gpu_temp_path = find_hwmon_temp_sensor(hwmon_name_hints, hwmon_label_hints);

        if (!gpu_temp_path) {
            const std::vector<std::string> zone_hints = {"gpu", "amdgpu", "nvidia"};
            gpu_temp_path = find_thermal_zone_by_type(zone_hints);
        }

        if (!gpu_temp_path && !gpu_sensor_warned.exchange(true)) {
            std::cerr << "better-auto: GPU thermal sensor not found; automatic mode will rely on CPU temperature" << std::endl;
        }
    });
    return gpu_temp_path;
}

static std::optional<std::string> locate_gpu_busy_file()
{
    std::call_once(gpu_usage_once, []() {
        DIR *dir = opendir("/sys/class/drm");
        if (!dir) {
            if (!gpu_usage_warned.exchange(true)) {
                std::cerr << "better-auto: /sys/class/drm unavailable; GPU usage tracking disabled" << std::endl;
            }
            return;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr)
        {
            if (std::strncmp(entry->d_name, "card", 4) != 0) {
                continue;
            }

            std::string candidate = std::string("/sys/class/drm/") + entry->d_name + "/device/gpu_busy_percent";
            std::ifstream test(candidate);
            if (test)
            {
                gpu_busy_path = candidate;
                break;
            }
        }

        closedir(dir);
        if (!gpu_busy_path && !gpu_usage_warned.exchange(true)) {
            std::cerr << "better-auto: GPU usage source not found; automatic mode will use temperature only" << std::endl;
        }
    });
    return gpu_busy_path;
}

static std::optional<double> read_temperature_celsius(const std::optional<std::string> &path)
{
    if (!path) {
        return std::nullopt;
    }

    std::ifstream file(*path);
    if (!file) {
        return std::nullopt;
    }

    long value = 0;
    file >> value;
    if (file.fail()) {
        return std::nullopt;
    }

    return static_cast<double>(value) / 1000.0;
}

static std::optional<double> read_cpu_usage_pct()
{
    std::ifstream stat_file("/proc/stat");
    if (!stat_file) {
        return std::nullopt;
    }

    std::string line;
    std::getline(stat_file, line);
    std::istringstream iss(line);

    std::string label;
    unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
    iss >> label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    if (label != "cpu") {
        return std::nullopt;
    }

    unsigned long long idle_all = idle + iowait;
    unsigned long long non_idle = user + nice + system + irq + softirq + steal;
    unsigned long long total = idle_all + non_idle;

    std::lock_guard<std::mutex> lock(cpu_usage_mutex);
    if (!previous_cpu_times) {
        previous_cpu_times = CpuSampleTimes{idle_all, total};
        return std::nullopt; // need a baseline before reporting usage
    }

    unsigned long long total_diff = total - previous_cpu_times->total;
    unsigned long long idle_diff = idle_all - previous_cpu_times->idle;
    previous_cpu_times = CpuSampleTimes{idle_all, total};

    if (total_diff == 0) {
        return std::nullopt;
    }

    double usage = static_cast<double>(total_diff - idle_diff) / static_cast<double>(total_diff);
    return usage * 100.0;
}

static std::optional<double> read_gpu_usage_pct()
{
    auto path = locate_gpu_busy_file();
    if (!path) {
        return std::nullopt;
    }

    std::ifstream file(*path);
    if (!file) {
        return std::nullopt;
    }

    double value = 0.0;
    file >> value;
    if (file.fail()) {
        return std::nullopt;
    }

    return value;
}

static ThermalSnapshot collect_snapshot()
{
    ThermalSnapshot snapshot;
    snapshot.cpu_temp_c = read_temperature_celsius(locate_cpu_temp_sensor());
    snapshot.gpu_temp_c = read_temperature_celsius(locate_gpu_temp_sensor());
    snapshot.cpu_usage_pct = read_cpu_usage_pct();
    snapshot.gpu_usage_pct = read_gpu_usage_pct();
    return snapshot;
}

static int fan_max_for_index(size_t index)
{
    std::call_once(fan_max_once[index], [index]() {
        std::string hwmon_path = find_hwmon_directory("/sys/devices/platform/hp-wmi/hwmon");
        if (!hwmon_path.empty()) {
            std::string path = hwmon_path + "/fan" + std::to_string(index + 1) + "_max";
            std::ifstream file(path);
            if (file) {
                int value = 0;
                file >> value;
                if (!file.fail() && value > 0) {
                    fan_max_cache[index] = value;
                    return;
                }
            }
        }
        fan_max_cache[index] = Config::BETTER_AUTO_MAX_FALLBACK[index];
    });
    return fan_max_cache[index];
}

static int clamp_to_fan_limits(size_t index, int rpm)
{
    int max_rpm = fan_max_for_index(index);
    if (rpm < 0) {
        return 0;
    }
    return std::min(rpm, max_rpm);
}

static int level_from_thresholds(double value, const std::array<double, 7> &thresholds)
{
    int level = 1;
    for (double threshold : thresholds) {
        if (value >= threshold) {
            ++level;
        }
    }
        return std::clamp(level, 1, Config::BETTER_AUTO_STEPS);
}

static int rpm_for_level_for_fan(int level, size_t fan_index)
{
        level = std::clamp(level, 1, Config::BETTER_AUTO_STEPS);
        int max_rpm = fan_max_for_index(fan_index);
        if (Config::BETTER_AUTO_STEPS <= 1) {
        return max_rpm;
    }

            double step = static_cast<double>(max_rpm - Config::BETTER_AUTO_MIN_RPM) / static_cast<double>(Config::BETTER_AUTO_STEPS - 1);
            double value = static_cast<double>(Config::BETTER_AUTO_MIN_RPM) + static_cast<double>(level - 1) * step;
            int rpm = static_cast<int>(std::round(value));
            rpm = std::clamp(rpm, Config::BETTER_AUTO_MIN_RPM, max_rpm);
    return rpm;
}

static std::array<int, 2> rpm_for_level(int level)
{
    return {rpm_for_level_for_fan(level, 0), rpm_for_level_for_fan(level, 1)};
}

static int level_from_snapshot(const ThermalSnapshot &snapshot, int previous_level)
{
        const auto& temp_thresholds = Config::TEMP_THRESHOLDS;
        const auto& usage_thresholds = Config::USAGE_THRESHOLDS;

    double hottest = 0.0;
    bool have_temp = false;
    if (snapshot.cpu_temp_c) {
        hottest = std::max(hottest, *snapshot.cpu_temp_c);
        have_temp = true;
    }
    if (snapshot.gpu_temp_c) {
        hottest = std::max(hottest, *snapshot.gpu_temp_c);
        have_temp = true;
    }

    int temp_level = have_temp ? level_from_thresholds(hottest, temp_thresholds) : previous_level;

    double usage_pct = 0.0;
    bool have_usage = false;
    if (snapshot.cpu_usage_pct) {
        usage_pct = std::max(usage_pct, *snapshot.cpu_usage_pct);
        have_usage = true;
    }
    if (snapshot.gpu_usage_pct) {
        usage_pct = std::max(usage_pct, *snapshot.gpu_usage_pct);
        have_usage = true;
    }

    int usage_level = have_usage ? level_from_thresholds(usage_pct, usage_thresholds) : 1;

    int target_level = std::max(temp_level, usage_level);
        target_level = std::clamp(target_level, 1, Config::BETTER_AUTO_STEPS);

    if (target_level < previous_level) {
        // Drop at most one step per sample to avoid oscillations
        target_level = std::max(target_level, previous_level - 1);
    }

    return target_level;
}

static void stop_better_auto();
static std::string start_better_auto();
static void better_auto_worker();

static std::string write_hw_fan_mode(const std::string &mode)
{
	std::string hwmon_path = find_hwmon_directory("/sys/devices/platform/hp-wmi/hwmon");

	if (!hwmon_path.empty())
	{
		std::ofstream fan_ctrl(hwmon_path + "/pwm1_enable");

		if (fan_ctrl)
		{
			if (mode == "AUTO")
				fan_ctrl << "2";
			else if (mode == "MANUAL")
				fan_ctrl << "1";
			else if (mode == "MAX")
				fan_ctrl << "0";
			else
				return "ERROR: Invalid fan mode: " + mode;

			fan_ctrl.flush();
			if (fan_ctrl.fail()) {
				return "ERROR: Failed to write fan mode";
			}
			return "OK";
		}
		else
			return "ERROR: Unable to set fan mode";
	}
	else
		return "ERROR: Hwmon directory not found";
}

static void better_auto_worker()
{
    LOG_INFO("better-auto: control loop started");
    int current_level = 3;
    auto last_apply = std::chrono::steady_clock::time_point::min();
    better_auto_last_manual_assert = std::chrono::steady_clock::time_point::min();

    while (better_auto_running.load(std::memory_order_acquire) && !global_shutdown.load(std::memory_order_acquire)) {
        ThermalSnapshot snapshot = collect_snapshot();
        int target_level = level_from_snapshot(snapshot, current_level);
        auto now = std::chrono::steady_clock::now();

        bool need_mode_refresh = (better_auto_last_manual_assert == std::chrono::steady_clock::time_point::min()) ||
                                 (now - better_auto_last_manual_assert >= std::chrono::seconds(80));
        if (need_mode_refresh) {
            auto refresh_result = write_hw_fan_mode("MANUAL");
            if (refresh_result != "OK") {
                std::cerr << "better-auto: failed to keep manual mode active: " << refresh_result << std::endl;
            }
            better_auto_last_manual_assert = now;
        }

        bool need_apply = (target_level != current_level) ||
                          (last_apply == std::chrono::steady_clock::time_point::min()) ||
                          (now - last_apply >= Config::BETTER_AUTO_REAPPLY);

        if (need_apply) {
            auto rpms = rpm_for_level(target_level);
            std::string rpm_str_fan1 = std::to_string(rpms[0]);
            std::string rpm_str_fan2 = std::to_string(rpms[1]);

            auto result1 = set_fan_speed("1", rpm_str_fan1, false, true);
            if (result1 != "OK") {
                std::cerr << "better-auto: failed to set fan 1 speed: " << result1 << std::endl;
            }

            const int gap_seconds = static_cast<int>(Config::FAN_APPLY_GAP.count());
            for (int i = 0; i < gap_seconds; ++i) {
                if (!better_auto_running.load(std::memory_order_acquire)) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            if (!better_auto_running.load(std::memory_order_acquire)) {
                break;
            }

            auto result2 = set_fan_speed("2", rpm_str_fan2, false, true);
            if (result2 != "OK") {
                std::cerr << "better-auto: failed to set fan 2 speed: " << result2 << std::endl;
            }

            current_level = target_level;
            last_apply = std::chrono::steady_clock::now();
        }

        const int tick_seconds = static_cast<int>(Config::BETTER_AUTO_TICK.count());
        for (int i = 0; i < tick_seconds; ++i) {
            if (!better_auto_running.load(std::memory_order_acquire)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    LOG_INFO("better-auto: control loop stopped");
}

static void stop_better_auto()
{
    if (better_auto_running.exchange(false, std::memory_order_acq_rel)) {
        if (better_auto_thread.joinable()) {
            better_auto_thread.join();
        }
    } else if (better_auto_thread.joinable()) {
        better_auto_thread.join();
    }
    better_auto_thread = std::thread();
}

static std::string start_better_auto()
{
    stop_better_auto();

    auto result = write_hw_fan_mode("MANUAL");
    if (result != "OK") {
        return result;
    }

    {
        std::lock_guard<std::mutex> lock(cpu_usage_mutex);
        previous_cpu_times.reset();
    }

    better_auto_running.store(true, std::memory_order_release);
    try {
        better_auto_thread = std::thread(better_auto_worker);
    } catch (const std::exception &ex) {
        better_auto_running.store(false, std::memory_order_release);
        std::cerr << "better-auto: failed to start worker thread: " << ex.what() << std::endl;
        return "ERROR: Unable to start better auto control thread";
    } catch (...) {
        better_auto_running.store(false, std::memory_order_release);
        std::cerr << "better-auto: failed to start worker thread (unknown error)" << std::endl;
        return "ERROR: Unable to start better auto control thread";
    }

    return "OK";
}

// Function to reapply fan settings without triggering fan_mode_trigger
void reapply_fan_settings() {
    bool expected = false;
    if (!is_reapplying.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
        return; // Another reapply loop is already running
    }

    std::optional<std::string> fan1_speed;
    std::optional<std::string> fan2_speed;
    {
        std::lock_guard<std::mutex> lock(fan_state_mutex);
        fan1_speed = last_fan1_speed;
        fan2_speed = last_fan2_speed;
    }

    std::string current_mode = get_fan_mode();
    if (current_mode == "MANUAL" && (fan1_speed || fan2_speed)) {
        std::ostringstream log_message;
        log_message << "Re-applying manual fan settings";
        bool has_detail = false;

        if (fan1_speed) {
            log_message << (has_detail ? ", " : ": ") << "fan1=" << *fan1_speed;
            has_detail = true;
        }
        if (fan2_speed) {
            log_message << (has_detail ? ", " : ": ") << "fan2=" << *fan2_speed;
        }

        std::cout << log_message.str() << std::endl;

        if (fan1_speed) {
            auto result = set_fan_speed("1", *fan1_speed, false, false);
            if (result != "OK") {
                std::cerr << "Failed to reapply fan 1 speed: " << result << std::endl;
            }
        }

        if (fan2_speed) {
            auto result = set_fan_speed("2", *fan2_speed, false, false);
            if (result != "OK") {
                std::cerr << "Failed to reapply fan 2 speed: " << result << std::endl;
            }
        }
    }

    is_reapplying.store(false, std::memory_order_release);
}

// call set_fan_mode every 90 seconds so that the mode doesn't revert back (weird hp behaviour)
// also re-applies manual fan speed
void fan_mode_trigger(const std::string mode) {
    fan_thread_generation++;
	if (mode == "AUTO" || mode == "BETTER_AUTO") return;

    std::thread([mode, gen = fan_thread_generation.load()]() {
        while (fan_thread_generation == gen && !global_shutdown.load(std::memory_order_acquire)) {
            // Reapply the fan mode directly via hwmon
            auto result = write_hw_fan_mode(mode);
            if (result != "OK") {
                LOG_ERROR("fan_mode_trigger: failed to assert mode " + mode + ": " + result);
            }

            // Reapply fan settings if in manual mode
            if (mode == "MANUAL") {
                reapply_fan_settings();
            }

            // Wait for the interval (90 seconds)
            for (int i = 0; i < 90; ++i) {
                if (fan_thread_generation != gen || global_shutdown.load(std::memory_order_acquire)) return;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        LOG_DEBUG("fan_mode_trigger thread exiting for mode " + mode);
    }).detach();
}

std::string get_fan_mode()
{
	{
		std::lock_guard<std::mutex> lock(mode_mutex);
		if (requested_mode == "BETTER_AUTO") {
			return requested_mode;
		}
	}

	std::string hwmon_path = find_hwmon_directory("/sys/devices/platform/hp-wmi/hwmon");

	if (!hwmon_path.empty())
	{
		std::string pwm_path = hwmon_path + "/pwm1_enable";
		std::ifstream fan_ctrl(pwm_path);

		if (fan_ctrl)
		{
			std::stringstream buffer;
			buffer << fan_ctrl.rdbuf();
			std::string fan_mode = buffer.str();

			fan_mode.erase(fan_mode.find_last_not_of(" \n\r\t") + 1);

			if (fan_mode == "2")
				return "AUTO";
			else if (fan_mode == "1")
				return "MANUAL";
			else if (fan_mode == "0")
				return "MAX";
			else
				return "ERROR: Unknown fan mode " + fan_mode;
		}
		else
		{
			std::cerr << "Failed to open fan control file. Error: " << strerror(errno) << std::endl;
			return "ERROR: Unable to read fan mode";
		}
	}
	else
	{
		std::cerr << "Hwmon directory not found" << std::endl;
		return "ERROR: Hwmon directory not found";
	}
}

std::string set_fan_mode(const std::string &mode)
{
    std::string previous_mode;
    {
        std::lock_guard<std::mutex> lock(mode_mutex);
        previous_mode = requested_mode;
    }
    bool entering_manual = (mode == "MANUAL" && previous_mode != "MANUAL");

    if (mode == "BETTER_AUTO") {
        auto result = start_better_auto();
        if (result == "OK") {
            std::lock_guard<std::mutex> lock(mode_mutex);
            requested_mode = "BETTER_AUTO";
        }
        return result;
    }

    stop_better_auto();

    auto result = write_hw_fan_mode(mode);
    if (result == "OK") {
        std::lock_guard<std::mutex> lock(mode_mutex);
        requested_mode = mode;
        if (entering_manual) {
            std::lock_guard<std::mutex> speed_lock(fan_state_mutex);
            last_fan1_speed.reset();
            last_fan2_speed.reset();
        }
    }
    return result;
}

std::string get_fan_speed(const std::string &fan_num)
{
	std::string hwmon_path = find_hwmon_directory("/sys/devices/platform/hp-wmi/hwmon");

	if (!hwmon_path.empty())
	{
		std::ifstream fan_file(hwmon_path + "/fan" + fan_num + "_input");

		if (fan_file)
		{
			std::stringstream buffer;
			buffer << fan_file.rdbuf();

			std::string fan_speed = buffer.str();

			fan_speed.erase(fan_speed.find_last_not_of(" \n\r\t") + 1);

			return fan_speed;
		}
		else
		{
			std::cerr << "Failed to open fan speed file. Error: " << strerror(errno) << std::endl;
			return "ERROR: Unable to read fan speed";
		}
	}
	else
	{
		std::cerr << "Hwmon directory not found" << std::endl;
		return "ERROR: Hwmon directory not found";
	}
}

std::string set_fan_speed(const std::string &fan_num, const std::string &speed, bool trigger_mode, bool update_cache)
{
    // Validate inputs first
    if (!Validation::validate_fan_number(fan_num)) {
        LOG_WARNING("Invalid fan number in set_fan_speed: " + fan_num);
        return "ERROR: Invalid fan number";
    }
    
    int parsed_speed;
    if (!Validation::validate_fan_speed(speed, parsed_speed)) {
        LOG_WARNING("Invalid fan speed in set_fan_speed: " + speed);
        return "ERROR: Invalid fan speed value";
    }

    size_t index = (fan_num == "2") ? 1 : 0;
    int clamped_speed = clamp_to_fan_limits(index, parsed_speed);
    if (clamped_speed != parsed_speed) {
        LOG_INFO("Clamped fan " + fan_num + " target from " + std::to_string(parsed_speed) + " to " + std::to_string(clamped_speed));
    }
    
    std::string clamped_str = std::to_string(clamped_speed);
    
    if (update_cache) {
        std::lock_guard<std::mutex> lock(fan_state_mutex);
        if (fan_num == "1") {
            last_fan1_speed = clamped_str;
        } else if (fan_num == "2") {
            last_fan2_speed = clamped_str;
        }
    }

    // Apply rate limiting
    std::unique_lock<std::mutex> apply_lock(fan_apply_mutex);
    auto now = std::chrono::steady_clock::now();
    if (index == 1 && fan_last_apply[0] != std::chrono::steady_clock::time_point::min()) {
        auto elapsed = now - fan_last_apply[0];
        if (elapsed < Config::FAN_APPLY_GAP) {
            auto wait_duration = Config::FAN_APPLY_GAP - elapsed;
            apply_lock.unlock();
            std::this_thread::sleep_for(wait_duration);
            apply_lock.lock();
        }
    }

    // Direct sysfs write instead of shell script
    std::string hwmon_path = find_hwmon_directory("/sys/devices/platform/hp-wmi/hwmon");
    if (hwmon_path.empty()) {
        LOG_ERROR("Hwmon directory not found for fan control");
        return "ERROR: Hwmon directory not found";
    }

    // Ensure manual mode is set before changing fan speed
    std::string pwm_enable_path = hwmon_path + "/pwm1_enable";
    std::ofstream pwm_enable(pwm_enable_path);
    if (!pwm_enable) {
        LOG_ERROR("Failed to open PWM enable file: " + pwm_enable_path);
        return "ERROR: Failed to set manual mode";
    }
    pwm_enable << "1";
    pwm_enable.close();

    // Write the fan speed
    std::string target_file = hwmon_path + "/fan" + fan_num + "_target";
    std::ofstream fan_target(target_file);
    if (!fan_target) {
        LOG_ERROR("Failed to open fan target file: " + target_file);
        return "ERROR: Failed to set fan speed";
    }
    
    fan_target << clamped_str;
    fan_target.flush();
    
    if (fan_target.fail()) {
        LOG_ERROR("Failed to write to fan target file: " + target_file);
        return "ERROR: Failed to write fan speed";
    }
    
    fan_target.close();
    fan_last_apply[index] = std::chrono::steady_clock::now();
    apply_lock.unlock();

    // Only trigger fan_mode_trigger if requested and not already reapplying
    if (trigger_mode && !is_reapplying.load(std::memory_order_acquire) && get_fan_mode() == "MANUAL") {
        fan_mode_trigger("MANUAL");
    }
    
    LOG_DEBUG("Successfully set fan " + fan_num + " speed to " + clamped_str);
    return "OK";
}

void fan_cleanup() {
    LOG_INFO("Starting fan subsystem cleanup");
    
    // Signal all threads to shutdown
    global_shutdown.store(true, std::memory_order_release);
    
    // Stop better auto if running
    stop_better_auto();
    
    // Increment generation to stop any fan_mode_trigger threads
    fan_thread_generation++;
    
    // Give threads time to exit gracefully
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    LOG_INFO("Fan subsystem cleanup completed");
}
