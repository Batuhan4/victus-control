#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/un.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <cerrno>

#include "fan.hpp"
#include "util.hpp"

static std::atomic<int> fan_thread_generation(0);

// call set_fan_mode every 100 seconds so that the mode doesn't revert back (weird hp behaviour)
void fan_mode_trigger(const std::string mode) {
    fan_thread_generation++;
	if (mode == "AUTO") return;

    std::thread([mode, gen = fan_thread_generation.load()]() {
        while (fan_thread_generation == gen) {
            std::string result = set_fan_mode(mode);
            if (result != "OK") {
                std::cerr << "Failed to reapply fan mode: " << result << std::endl;
            }
            for (int i = 0; i < 100; ++i) {
                if (fan_thread_generation != gen) return;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }).detach();
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

std::string set_fan_speed(const std::string &fan_num, const std::string &speed)
{
	std::cout << "Setting fan " << fan_num << " speed to " << speed << std::endl;
	std::string hwmon_path = find_hwmon_directory("/sys/devices/platform/hp-wmi/hwmon");

	if (!hwmon_path.empty())
	{
		std::ofstream fan_file(hwmon_path + "/fan" + fan_num + "_target");

		if (fan_file)
		{
			fan_file << speed;
			fan_file.flush();
			if (fan_file.fail()) {
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
