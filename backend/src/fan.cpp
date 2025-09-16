#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/un.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>

#include "fan.hpp"
#include "util.hpp"
#include "constants.hpp"

// Thread management structure
struct FanThreadManager {
	std::thread worker_thread;
	std::atomic<bool> should_stop{false};
	std::mutex manager_mutex;
	bool thread_active = false;
	
	void stop_and_join() {
		std::lock_guard<std::mutex> lock(manager_mutex);
		if (thread_active) {
			should_stop = true;
			if (worker_thread.joinable()) {
				worker_thread.join();
			}
			thread_active = false;
		}
	}
	
	void start_new_worker(const std::string &mode) {
		std::lock_guard<std::mutex> lock(manager_mutex);
		stop_and_join_unsafe();
		
		should_stop = false;
		worker_thread = std::thread([this, mode]() {
			while (!should_stop) {
				if (should_stop) break;
				set_fan_mode(mode);
				
				// Sleep for FAN_REAPPLY_SECONDS, checking should_stop every second
				for (int i = 0; i < FAN_REAPPLY_SECONDS && !should_stop; ++i) {
					std::this_thread::sleep_for(std::chrono::seconds(1));
				}
			}
		});
		thread_active = true;
	}
	
private:
	void stop_and_join_unsafe() {
		if (thread_active) {
			should_stop = true;
			if (worker_thread.joinable()) {
				worker_thread.join();
			}
			thread_active = false;
		}
	}
};

static FanThreadManager fan_thread_manager;

// call set_fan_mode every 100 seconds so that the mode doesn't revert back (weird hp behaviour)
void fan_mode_trigger(const std::string mode) {
	if (mode == "AUTO") {
		fan_thread_manager.stop_and_join();
		return;
	}
	
	log(LogLevel::Info, "Starting fan mode trigger for mode: " + mode);
	fan_thread_manager.start_new_worker(mode);
}

std::string get_fan_mode()
{
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
			log(LogLevel::Error, "Failed to open fan control file: " + std::string(strerror(errno)));
			return "ERROR: Unable to read fan mode";
		}
	}
	else
	{
		log(LogLevel::Error, "Hwmon directory not found");
		return "ERROR: Hwmon directory not found";
	}
}

std::string set_fan_mode(const std::string &mode)
{
	// Input validation
	if (!is_valid_fan_mode(mode)) {
		log(LogLevel::Warn, "Invalid fan mode provided: " + mode);
		return "ERROR: Invalid fan mode: " + mode;
	}
	
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

			fan_ctrl.flush();
			if (fan_ctrl.fail()) {
				log(LogLevel::Error, "Failed to write fan mode");
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
			log(LogLevel::Error, "Failed to open fan speed file: " + std::string(strerror(errno)));
			return "ERROR: Unable to read fan speed";
		}
	}
	else
	{
		log(LogLevel::Error, "Hwmon directory not found");
		return "ERROR: Hwmon directory not found";
	}
}

std::string set_fan_speed(const std::string &fan_num, const std::string &speed)
{
	// Input validation
	if (!is_valid_fan_speed(speed)) {
		log(LogLevel::Warn, "Invalid fan speed provided: " + speed);
		return "ERROR: Invalid fan speed: " + speed;
	}
	
	log(LogLevel::Info, "Setting fan " + fan_num + " speed to " + speed);
	std::string hwmon_path = find_hwmon_directory("/sys/devices/platform/hp-wmi/hwmon");

	if (!hwmon_path.empty())
	{
		std::ofstream fan_file(hwmon_path + "/fan" + fan_num + "_target");

		if (fan_file)
		{
			fan_file << speed;
			fan_file.flush();
			if (fan_file.fail()) {
				log(LogLevel::Error, "Failed to write fan speed");
				return "ERROR: Failed to write fan speed";
			}
			return "OK";
		}
		else
			return "ERROR: Unable to set fan speed";
	}
	else
		return "ERROR: Hwmon directory not found";
}
// Cleanup function for graceful shutdown
void cleanup_fan_threads()
{
	log(LogLevel::Info, "Cleaning up fan threads");
	fan_thread_manager.stop_and_join();
}
