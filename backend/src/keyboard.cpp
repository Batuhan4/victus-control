#include <fstream>
#include <sstream>

#include "keyboard.hpp"

std::string get_keyboard_color()
{
	std::ifstream rgb("/sys/class/leds/hp::kbd_backlight/multi_intensity");

	if (rgb)
	{
		std::stringstream buffer;
		buffer << rgb.rdbuf();

		std::string rgb_mode = buffer.str();
		size_t pos = rgb_mode.find_last_not_of(" \n\r\t");
		if (pos == std::string::npos) rgb_mode.clear(); else rgb_mode.erase(pos + 1);

		return rgb_mode;
	}
	else
		return "ERROR: RGB File not found";
}

std::string set_keyboard_color(const std::string &color)
{
	std::ofstream rgb("/sys/class/leds/hp::kbd_backlight/multi_intensity");
	if (rgb)
	{
		rgb << color;
		rgb.flush();
		if (rgb.fail()) {
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
        size_t pos = keyboard_brightness.find_last_not_of(" \n\r\t");
        if (pos == std::string::npos) keyboard_brightness.clear(); else keyboard_brightness.erase(pos + 1);

        return keyboard_brightness;
    }
    else
        return "ERROR: Keyboard Brightness File not found";
}

std::string set_keyboard_brightness(const std::string &value)
{
    std::ofstream brightness("/sys/class/leds/hp::kbd_backlight/brightness");
    if (brightness)
    {
        brightness << value;
        brightness.flush();
        if (brightness.fail()) {
            return "ERROR: Failed to write keyboard brightness";
        }
        return "OK";
    }
    else
        return "ERROR: Keyboard Brightness File not found";
}
