#include "util.hpp"
#include <dirent.h>
#include <string>
#include <iostream>
#include <regex>

std::string find_hwmon_directory(const std::string &base_path)
{
	DIR *dir;
	struct dirent *ent;
	std::string hwmon_path;
	int max_hwmon = -1;

	if ((dir = opendir(base_path.c_str())) != nullptr)
	{
		while ((ent = readdir(dir)) != nullptr)
		{
			std::string name = ent->d_name;
			if (name.find("hwmon") == 0 && name.size() > 5)
			{
				try
				{
					int num = std::stoi(name.substr(5));
					if (num > max_hwmon)
					{
						max_hwmon = num;
						hwmon_path = base_path + "/" + name;
					}
				}
				catch (const std::exception &)
				{
					// Ignore invalid names
				}
			}
		}
		closedir(dir);
	}
	return hwmon_path;
}

// Logging implementation
void log(LogLevel lvl, const std::string &msg)
{
	std::string prefix;
	switch (lvl) {
		case LogLevel::Info:  prefix = "[INFO] "; break;
		case LogLevel::Warn:  prefix = "[WARN] "; break;
		case LogLevel::Error: prefix = "[ERROR] "; break;
		case LogLevel::Debug: prefix = "[DEBUG] "; break;
	}
	
	if (lvl == LogLevel::Error) {
		std::cerr << prefix << msg << std::endl;
	} else {
		std::clog << prefix << msg << std::endl;
	}
}

// Input validation implementations
bool is_valid_fan_mode(const std::string &mode)
{
	return mode == "AUTO" || mode == "MANUAL" || mode == "MAX";
}

bool is_valid_fan_speed(const std::string &speed)
{
	if (speed.empty()) return false;
	
	try {
		int rpm = std::stoi(speed);
		// Valid RPM range for HP Victus fans (reasonable bounds)
		return rpm >= 0 && rpm <= 6000;
	} catch (const std::exception &) {
		return false;
	}
}

bool is_valid_hex_rgb(const std::string &rgb)
{
	if (rgb.length() != 6) return false;
	
	std::regex hex_pattern("^[0-9A-Fa-f]{6}$");
	return std::regex_match(rgb, hex_pattern);
}

bool is_valid_brightness(const std::string &brightness)
{
	if (brightness.empty()) return false;
	
	try {
		int level = std::stoi(brightness);
		// HP keyboard brightness typically 0-3
		return level >= 0 && level <= 3;
	} catch (const std::exception &) {
		return false;
	}
}
