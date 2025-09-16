#include <fstream>
#include <sstream>

#include "keyboard.hpp"
#include "util.hpp"

std::string get_keyboard_color()
{
	std::ifstream rgb("/sys/class/leds/hp::kbd_backlight/multi_intensity");

	if (rgb)
	{
		std::stringstream buffer;
		buffer << rgb.rdbuf();

		std::string rgb_mode = buffer.str();
		rgb_mode.erase(rgb_mode.find_last_not_of(" \n\r\t") + 1);

		return rgb_mode;
	}
	else
		return "ERROR: RGB File not found";
}

std::string set_keyboard_color(const std::string &color)
{
	// Parse and validate RGB color format (expects "R G B" format)
	std::istringstream iss(color);
	std::string r, g, b;
	iss >> r >> g >> b;
	
	// Basic validation - ensure we have three values
	if (r.empty() || g.empty() || b.empty()) {
		log(LogLevel::Warn, "Invalid RGB color format: " + color);
		return "ERROR: Invalid RGB color format";
	}
	
	// Additional validation for numeric values
	try {
		int rv = std::stoi(r), gv = std::stoi(g), bv = std::stoi(b);
		if (rv < 0 || rv > 255 || gv < 0 || gv > 255 || bv < 0 || bv > 255) {
			log(LogLevel::Warn, "RGB values out of range: " + color);
			return "ERROR: RGB values must be 0-255";
		}
	} catch (const std::exception &) {
		log(LogLevel::Warn, "Invalid numeric RGB values: " + color);
		return "ERROR: Invalid numeric RGB values";
	}
	
	std::ofstream rgb("/sys/class/leds/hp::kbd_backlight/multi_intensity");
	if (rgb)
	{
		rgb << color;
		rgb.flush();
		if (rgb.fail()) {
			log(LogLevel::Error, "Failed to write RGB color");
			return "ERROR: Failed to write RGB color";
		}
		return "OK";
	}
	else
		return "ERROR: RGB File not found";
}


std::string get_keyboard_brightness()
{
    std::ifstream brightness("/sys/class/leds/hp::kbd_backlight/brightness");

    if (brightness)
    {
        std::stringstream buffer;
        buffer << brightness.rdbuf();

        std::string keyboard_brightness = buffer.str();
        keyboard_brightness.erase(keyboard_brightness.find_last_not_of(" \n\r\t") + 1);

        return keyboard_brightness;
    }
    else
        return "ERROR: Keyboard Brightness File not found";
}

std::string set_keyboard_brightness(const std::string &value)
{
	// Input validation
	if (!is_valid_brightness(value)) {
		log(LogLevel::Warn, "Invalid brightness value: " + value);
		return "ERROR: Invalid brightness value: " + value;
	}
	
    std::ofstream brightness("/sys/class/leds/hp::kbd_backlight/brightness");
    if (brightness)
    {
        brightness << value;
        brightness.flush();
        if (brightness.fail()) {
            log(LogLevel::Error, "Failed to write keyboard brightness");
            return "ERROR: Failed to write keyboard brightness";
        }
        return "OK";
    }
    else
        return "ERROR: Keyboard Brightness File not found";
}
