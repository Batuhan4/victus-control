#pragma once
#include <string>

std::string find_hwmon_directory(const std::string &base_path);

// Logging utilities
enum class LogLevel { Info, Warn, Error, Debug };
void log(LogLevel lvl, const std::string &msg);

// Input validation utilities
bool is_valid_fan_mode(const std::string &mode);
bool is_valid_fan_speed(const std::string &speed);
bool is_valid_hex_rgb(const std::string &rgb);
bool is_valid_brightness(const std::string &brightness);

// Error handling utilities
inline bool is_error(const std::string &s) { return s.rfind("ERROR:", 0) == 0; }