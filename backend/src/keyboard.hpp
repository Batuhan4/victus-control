#include <string>

std::string get_keyboard_type();
std::string get_keyboard_color();
std::string get_keyboard_zone_color(int zone);
std::string set_keyboard_color(const std::string &color);
std::string set_keyboard_zone_color(int zone, const std::string &color);

std::string get_keyboard_brightness();
std::string set_keyboard_brightness(const std::string &value);
