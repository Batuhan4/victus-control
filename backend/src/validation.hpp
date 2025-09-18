#ifndef VALIDATION_HPP
#define VALIDATION_HPP

#include <string>
#include <sstream>
#include <regex>
#include "config.hpp"

namespace Validation {
    
    inline bool validate_fan_number(const std::string& fan_num) {
        return fan_num == "1" || fan_num == "2";
    }
    
    inline bool validate_fan_speed(const std::string& speed, int& parsed_speed) {
        try {
            parsed_speed = std::stoi(speed);
            return parsed_speed >= 0 && parsed_speed <= Config::MAX_ALLOWED_RPM;
        } catch (const std::exception&) {
            return false;
        }
    }
    
    inline bool validate_fan_mode(const std::string& mode) {
        return mode == "AUTO" || mode == "MANUAL" || mode == "MAX" || mode == "BETTER_AUTO";
    }
    
    inline bool validate_rgb_color(const std::string& color_str, int& r, int& g, int& b) {
        std::istringstream iss(color_str);
        if (!(iss >> r >> g >> b)) {
            return false;
        }
        
        // Check for extra input
        std::string extra;
        if (iss >> extra) {
            return false;
        }
        
        return r >= Config::MIN_RGB_VALUE && r <= Config::MAX_RGB_VALUE &&
               g >= Config::MIN_RGB_VALUE && g <= Config::MAX_RGB_VALUE &&
               b >= Config::MIN_RGB_VALUE && b <= Config::MAX_RGB_VALUE;
    }
    
    inline bool validate_brightness(const std::string& brightness_str, int& brightness) {
        try {
            brightness = std::stoi(brightness_str);
            return brightness >= Config::MIN_BRIGHTNESS && brightness <= Config::MAX_BRIGHTNESS;
        } catch (const std::exception&) {
            return false;
        }
    }
    
    // Sanitize strings to prevent any injection attacks
    inline std::string sanitize_string(const std::string& input) {
        // Remove any potentially dangerous characters
        std::string sanitized;
        for (char c : input) {
            if (std::isalnum(c) || c == ' ' || c == '_' || c == '-') {
                sanitized += c;
            }
        }
        return sanitized;
    }
}

#endif // VALIDATION_HPP