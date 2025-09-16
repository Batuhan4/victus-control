#pragma once
#include <string>

void fan_mode_trigger(const std::string mode);
std::string set_fan_mode(const std::string &mode);
std::string get_fan_mode();

std::string get_fan_speed(const std::string &fan_num);
std::string set_fan_speed(const std::string &fan_num, const std::string &speed);

// Cleanup function for graceful shutdown
void cleanup_fan_threads();

