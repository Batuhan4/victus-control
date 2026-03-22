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
#include "validation.hpp"

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
constexpr const char *kSudoPath = "/usr/bin/sudo";

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
  if (hex.size() != 6)
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

  for (char ch : hex) {
    if (!std::isxdigit(static_cast<unsigned char>(ch)))
      return false;
  }

  return true;
}

int run_helper_command(const std::vector<std::string> &args) {
  if (args.empty()) {
    errno = EINVAL;
    return -1;
  }

  std::vector<char *> argv;
  argv.reserve(args.size() + 1);
  for (const auto &arg : args) {
    argv.push_back(const_cast<char *>(arg.c_str()));
  }
  argv.push_back(nullptr);

  pid_t pid = fork();
  if (pid < 0)
    return -1;

  if (pid == 0) {
    execv(args.front().c_str(), argv.data());
    _exit(127);
  }

  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR)
      return -1;
  }

  return status;
}

std::string write_rgb_zone_with_helper(int zone, const std::string &hex_color) {
  if (zone < 0 || zone >= kFourZoneCount)
    return "ERROR: Invalid zone number";

  if (!is_valid_hex_color(hex_color))
    return "ERROR: Invalid hex color value";

  int status = run_helper_command(
      {kSudoPath, kRgbZoneWriterPath, std::to_string(zone), hex_color});
  if (status == 0)
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
  std::array<int, 3> rgb_values;
  if (!parse_rgb_triplet(color, &rgb_values))
    return "ERROR: Invalid RGB color";

  std::string canonical_color = std::to_string(rgb_values[0]) + " " +
                                std::to_string(rgb_values[1]) + " " +
                                std::to_string(rgb_values[2]);

  if (omen_4zone_exists()) {
    std::string hex_val = rgb_triplet_to_hex(canonical_color);
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
    rgb << canonical_color;
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

  std::array<int, 3> rgb_values;
  if (!parse_rgb_triplet(color, &rgb_values))
    return "ERROR: Invalid RGB color";

  std::string canonical_color = std::to_string(rgb_values[0]) + " " +
                                std::to_string(rgb_values[1]) + " " +
                                std::to_string(rgb_values[2]);

  if (omen_4zone_exists()) {
    std::string hex_val = rgb_triplet_to_hex(canonical_color);
    if (hex_val.empty())
      return "ERROR: Invalid RGB color";

    return write_rgb_zone_with_helper(zone, hex_val);
  }

  return set_keyboard_color(canonical_color);
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
  int brightness_value = 0;
  if (!parse_bounded_int(value, 0, 255, &brightness_value))
    return "ERROR: Invalid keyboard brightness";

  if (omen_4zone_exists())
    return "OK";

  std::ofstream brightness(kSingleZoneBrightnessPath);
  if (brightness) {
    brightness << brightness_value;
    brightness.flush();
    if (brightness.fail())
      return "ERROR: Failed to write keyboard brightness";

    return "OK";
  }

  return "ERROR: Keyboard Brightness File not found";
}
