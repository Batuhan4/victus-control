#include "util.hpp"
#include <dirent.h>
#include <string>

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
