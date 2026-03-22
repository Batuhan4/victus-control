#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "keyboard.hpp"

namespace {

constexpr int kFourZoneCount = 4;
constexpr const char *kFourZoneZone0Path =
    "/sys/devices/platform/hp-wmi/rgb_zones/zone00";
constexpr const char *kFourZoneZonePathPrefix =
    "/sys/devices/platform/hp-wmi/rgb_zones/zone0";
constexpr const char *kSingleZoneColorPath =
    "/sys/class/leds/hp::kbd_backlight/multi_intensity";
constexpr const char *kSingleZoneBrightnessPath =
    "/sys/class/leds/hp::kbd_backlight/brightness";
constexpr const char *kRgbZoneWriterPath = "/usr/bin/set-rgb-zone.sh";

bool omen_4zone_exists() {
  struct stat buffer;
  return stat(kFourZoneZone0Path, &buffer) == 0;
}

std::string trim_trailing_whitespace(std::string value) {
  size_t last = value.find_last_not_of(" \n\r\t");
  if (last == std::string::npos)
    return "";

  value.erase(last + 1);
  return value;
}

std::string fourzone_zone_path(int zone) {
  return std::string(kFourZoneZonePathPrefix) + std::to_string(zone);
}

bool parse_hex_color(const std::string &hex, std::array<int, 3> *rgb) {
  if (hex.size() < 6)
    return false;

  try {
    (*rgb)[0] = std::stoi(hex.substr(0, 2), nullptr, 16);
    (*rgb)[1] = std::stoi(hex.substr(2, 2), nullptr, 16);
    (*rgb)[2] = std::stoi(hex.substr(4, 2), nullptr, 16);
  } catch (...) {
    return false;
  }

  return true;
}

bool parse_rgb_triplet(const std::string &color, std::array<int, 3> *rgb) {
  std::stringstream ss(color);
  int red;
  int green;
  int blue;
  char extra;

  if (!(ss >> red >> green >> blue))
    return false;

  if (ss >> extra)
    return false;

  if (red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 ||
      blue > 255)
    return false;

  *rgb = {red, green, blue};
  return true;
}

std::string hex_to_rgb_string(const std::string &hex) {
  std::array<int, 3> rgb;

  if (!parse_hex_color(hex, &rgb))
    return "255 255 255";

  return std::to_string(rgb[0]) + " " + std::to_string(rgb[1]) + " " +
         std::to_string(rgb[2]);
}

std::string rgb_triplet_to_hex(const std::string &color) {
  std::array<int, 3> rgb;

  if (!parse_rgb_triplet(color, &rgb))
    return "";

  std::ostringstream hex;
  hex << std::uppercase << std::setfill('0') << std::hex << std::setw(2)
      << rgb[0] << std::setw(2) << rgb[1] << std::setw(2) << rgb[2];
  return hex.str();
}

std::string read_text_file(const std::string &path) {
  std::ifstream file(path);
  if (!file)
    return "";

  std::stringstream buffer;
  buffer << file.rdbuf();
  return trim_trailing_whitespace(buffer.str());
}

bool is_valid_hex_color(const std::string &hex) {
  if (hex.size() != 6)
    return false;
  for (char c : hex) {
    if (!std::isxdigit(static_cast<unsigned char>(c)))
      return false;
  }
  return true;
}

std::string write_rgb_zone_with_helper(int zone, const std::string &hex_color) {
  if (zone < 0 || zone > 3)
    return "ERROR: Invalid zone number";

  if (!is_valid_hex_color(hex_color))
    return "ERROR: Invalid hex color value";

  std::string zone_str = std::to_string(zone);

  pid_t pid = fork();
  if (pid == -1)
    return "ERROR: Failed to fork helper process";

  if (pid == 0) {
    const char *argv[] = {"sudo", kRgbZoneWriterPath, zone_str.c_str(),
                          hex_color.c_str(), nullptr};
    execvp("sudo", const_cast<char *const *>(argv));
    _exit(127);
  }

  int status = 0;
  while (waitpid(pid, &status, 0) == -1) {
    if (errno != EINTR)
      return "ERROR: Failed waiting for helper process";
  }

  if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    return "OK";

  return "ERROR: Failed to set zone color";
}

std::string fourzone_brightness_value() {
  bool any_enabled = false;

  for (int zone = 0; zone < kFourZoneCount; zone++) {
    std::array<int, 3> rgb;
    std::string hex = read_text_file(fourzone_zone_path(zone));
    if (hex.empty() || !parse_hex_color(hex, &rgb))
      return "ERROR: Failed to read zone color";

    if (rgb[0] != 0 || rgb[1] != 0 || rgb[2] != 0)
      any_enabled = true;
  }

  return any_enabled ? "255" : "0";
}

} // namespace

std::string get_keyboard_type() {
  return omen_4zone_exists() ? "FOUR_ZONE" : "SINGLE_ZONE";
}

std::string get_keyboard_color() {
  if (omen_4zone_exists()) {
    std::string hex_val = read_text_file(kFourZoneZone0Path);
    if (!hex_val.empty())
      return hex_to_rgb_string(hex_val);
  }

  std::string rgb_mode = read_text_file(kSingleZoneColorPath);
  if (!rgb_mode.empty()) {
    return rgb_mode;
  }

  return "ERROR: RGB File not found";
}

std::string get_keyboard_zone_color(int zone) {
  if (zone < 0 || zone >= kFourZoneCount)
    return "ERROR: Invalid zone";

  if (omen_4zone_exists()) {
    std::string hex_val = read_text_file(fourzone_zone_path(zone));
    if (!hex_val.empty())
      return hex_to_rgb_string(hex_val);

    return "ERROR: Zone file not found";
  }

  return get_keyboard_color();
}

std::string set_keyboard_color(const std::string &color) {
  if (omen_4zone_exists()) {
    std::string hex_val = rgb_triplet_to_hex(color);
    if (hex_val.empty())
      return "ERROR: Invalid RGB color";

    for (int zone = 0; zone < kFourZoneCount; zone++) {
      std::string result = write_rgb_zone_with_helper(zone, hex_val);
      if (result != "OK")
        return result;
    }

    return "OK";
  }

  std::ofstream rgb(kSingleZoneColorPath);
  if (rgb) {
    rgb << color;
    rgb.flush();
    if (rgb.fail())
      return "ERROR: Failed to write RGB color";

    return "OK";
  }

  return "ERROR: RGB File not found";
}

std::string set_keyboard_zone_color(int zone, const std::string &color) {
  if (zone < 0 || zone >= kFourZoneCount)
    return "ERROR: Invalid zone";

  if (omen_4zone_exists()) {
    std::string hex_val = rgb_triplet_to_hex(color);
    if (hex_val.empty())
      return "ERROR: Invalid RGB color";

    return write_rgb_zone_with_helper(zone, hex_val);
  }

  return set_keyboard_color(color);
}

std::string get_keyboard_brightness() {
  if (omen_4zone_exists())
    return fourzone_brightness_value();

  std::ifstream brightness(kSingleZoneBrightnessPath);
  if (brightness) {
    return read_text_file(kSingleZoneBrightnessPath);
  }

  return "ERROR: Keyboard Brightness File not found";
}

std::string set_keyboard_brightness(const std::string &value) {
  if (omen_4zone_exists())
    return "OK";

  std::ofstream brightness(kSingleZoneBrightnessPath);
  if (brightness) {
    brightness << value;
    brightness.flush();
    if (brightness.fail())
      return "ERROR: Failed to write keyboard brightness";

    return "OK";
  }

  return "ERROR: Keyboard Brightness File not found";
}
