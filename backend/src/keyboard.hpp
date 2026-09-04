#ifndef VICTUS_KEYBOARD_HPP
#define VICTUS_KEYBOARD_HPP

#include <array>
#include <string>
#include <vector>

std::string get_keyboard_type();
std::string get_keyboard_color();
std::string get_keyboard_zone_color(int zone);
std::string set_keyboard_color(const std::string &color);
std::string set_keyboard_zone_color(int zone, const std::string &color);

std::string get_keyboard_brightness();
std::string set_keyboard_brightness(const std::string &value);

// --- Entry points for the animation engine (effects.cpp) -------------------
// These write straight to the hardware without stopping the running effect,
// which the public setters above deliberately do.

// 4 on Omen four-zone boards, 1 on single-zone backlights.
int keyboard_zone_count();

// rgb_triplets must hold keyboard_zone_count() entries, ordered by zone index.
// On four-zone hardware all four zones are pushed in one shot so a frame costs
// one write rather than four.
std::string write_keyboard_colors_raw(const std::vector<std::string> &rgb_triplets);

// Current colour as {r,g,b}, used to seed BREATHE. Falls back to white.
std::array<int, 3> current_keyboard_rgb();

#endif // VICTUS_KEYBOARD_HPP
