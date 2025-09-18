#include "util.hpp"
#include <dirent.h>
#include <string>
#include <unordered_map>
#include <mutex>

// Cache for hwmon paths
static std::unordered_map<std::string, std::string> hwmon_cache;
static std::mutex cache_mutex;

std::string find_hwmon_directory(const std::string &base_path)
{
	// Check cache first
	{
		std::lock_guard<std::mutex> lock(cache_mutex);
		auto it = hwmon_cache.find(base_path);
		if (it != hwmon_cache.end()) {
			return it->second;
		}
	}
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
	
	// Cache the result
	if (!hwmon_path.empty()) {
		std::lock_guard<std::mutex> lock(cache_mutex);
		hwmon_cache[base_path] = hwmon_path;
	}
	
	return hwmon_path;
}
