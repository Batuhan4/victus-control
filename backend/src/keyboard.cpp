#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <vector>

#include "keyboard.hpp"

// Helper: Check if Omen 4-zone driver files exist
static bool omen_4zone_exists() {
  struct stat buffer;
  return (stat("/sys/devices/platform/hp-wmi/rgb_zones/zone00", &buffer) == 0);
}

std::string get_keyboard_type() {
  if (omen_4zone_exists()) {
    return "FOUR_ZONE";
  } else {
    return "SINGLE_ZONE";
  }
}

// Helper: Convert Hex string (e.g. "FF0000") to "255 0 0" for the Frontend
static std::string hex_to_rgb_string(const std::string &hex) {
  if (hex.length() < 6)
    return "255 255 255";
  int r, g, b;
  std::stringstream ss;
  ss << std::hex << hex.substr(0, 2);
  ss >> r;
  ss.clear();
  ss << std::hex << hex.substr(2, 2);
  ss >> g;
  ss.clear();
  ss << std::hex << hex.substr(4, 2);
  ss >> b;
  return std::to_string(r) + " " + std::to_string(g) + " " + std::to_string(b);
}

std::string get_keyboard_color() {
  if (omen_4zone_exists()) {
    // Read Zone 0 as the "Global" color representative
    std::ifstream zone("/sys/devices/platform/hp-wmi/rgb_zones/zone00");
    if (zone) {
      std::string hex_val;
      zone >> hex_val;
      return hex_to_rgb_string(hex_val);
    }
  }

  // Fallback: Original Victus Single-Zone Logic
  std::ifstream rgb("/sys/class/leds/hp::kbd_backlight/multi_intensity");
  if (rgb) {
    std::stringstream buffer;
    buffer << rgb.rdbuf();
    std::string rgb_mode = buffer.str();
    // Clean up whitespace
    size_t last = rgb_mode.find_last_not_of(" \n\r\t");
    if (last != std::string::npos)
      rgb_mode.erase(last + 1);
    return rgb_mode;
  } else
    return "ERROR: RGB File not found";
}

std::string get_keyboard_zone_color(int zone) {
  if (zone < 0 || zone > 3) {
    return "ERROR: Invalid zone";
  }

  if (omen_4zone_exists()) {
    std::string path =
        "/sys/devices/platform/hp-wmi/rgb_zones/zone0" + std::to_string(zone);
    std::ifstream zone_file(path);
    if (zone_file) {
      std::string hex_val;
      zone_file >> hex_val;
      return hex_to_rgb_string(hex_val);
    }
    return "ERROR: Zone file not found";
  }

  // For single-zone, just return the global color
  return get_keyboard_color();
}

std::string set_keyboard_color(const std::string &color) {
  if (omen_4zone_exists()) {
    // Frontend sends "R G B" (e.g. "255 0 0")
    // We must convert to HEX "FF0000" and write to ALL 4 zones
    std::stringstream ss(color);
    int r, g, b;
    ss >> r >> g >> b;

    std::stringstream hex_ss;
    hex_ss << std::uppercase << std::setfill('0') << std::setw(2) << std::hex
           << r << std::setw(2) << g << std::setw(2) << b;

    std::string hex_val = hex_ss.str();

    // Write to zone00 through zone03
    for (int i = 0; i < 4; i++) {
      std::string path =
          "/sys/devices/platform/hp-wmi/rgb_zones/zone0" + std::to_string(i);
      std::ofstream zone(path);
      if (zone) {
        zone << hex_val;
      }
    }
    return "OK";
  }

  // Fallback: Original Victus Single-Zone Logic
  std::ofstream rgb("/sys/class/leds/hp::kbd_backlight/multi_intensity");
  if (rgb) {
    rgb << color;
    rgb.flush();
    if (rgb.fail()) {
      return "ERROR: Failed to write RGB color";
    }
    return "OK";
  } else
    return "ERROR: RGB File not found";
}

std::string set_keyboard_zone_color(int zone, const std::string &color) {
  if (zone < 0 || zone > 3) {
    return "ERROR: Invalid zone";
  }

  if (omen_4zone_exists()) {
    std::stringstream ss(color);
    int r, g, b;
    ss >> r >> g >> b;

    std::stringstream hex_ss;
    hex_ss << std::uppercase << std::setfill('0') << std::setw(2) << std::hex
           << r << std::setw(2) << g << std::setw(2) << b;
    std::string hex_val = hex_ss.str();

    // Use sudo helper script to write with elevated privileges
    std::string cmd = "sudo /usr/bin/set-rgb-zone.sh " + std::to_string(zone) +
                      " " + hex_val + " 2>&1";
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
      return "ERROR: Failed to execute helper script";
    }

    char buffer[256];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      result += buffer;
    }
    int ret = pclose(pipe);

    if (ret == 0) {
      return "OK";
    }
    return "ERROR: " +
           (result.empty() ? std::string("Failed to set zone color") : result);
  }

  // Fallback to setting global color
  return set_keyboard_color(color);
}

std::string get_keyboard_brightness() {
  // Omen 4-zone brightness is often controlled by the color itself (black =
  // off) But we check the standard path just in case
  std::ifstream brightness("/sys/class/leds/hp::kbd_backlight/brightness");
  if (brightness) {
    std::stringstream buffer;
    buffer << brightness.rdbuf();
    std::string val = buffer.str();
    size_t last = val.find_last_not_of(" \n\r\t");
    if (last != std::string::npos)
      val.erase(last + 1);
    return val;
  }
  return "255"; // Fake it for Omen if file missing, assuming always on
}

std::string set_keyboard_brightness(const std::string &value)
{
  std::ofstream brightness("/sys/class/leds/hp::kbd_backlight/brightness");
    if (brightness)
    {
    brightness << value;
    return "OK";
  }
  // If file doesn't exist (Omen 4-zone often handles brightness via color),
  // we just return OK so the UI doesn't complain.
  return "OK";
}