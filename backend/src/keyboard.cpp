#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "effects.hpp"
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

// Colors stashed when a four-zone keyboard is toggled off, so it can be
// restored on toggle-on. Empty until the first off with lit zones.
// Guarded by g_fourzone_mutex: each client is served on its own detached
// thread, so concurrent SET_KBD_BRIGHTNESS calls would otherwise race on this
// global (and interleave the per-zone hardware writes).
std::optional<std::array<std::string, kFourZoneCount>> g_fourzone_saved_colors;
std::mutex g_fourzone_mutex;

// Explicit power state for four-zone boards, which have no brightness knob of
// their own. Set by SET_KBD_BRIGHTNESS 0, cleared by a non-zero brightness or
// an explicit colour write. The animation engine drops its frames while this
// is set (see write_keyboard_colors_raw), so switching the backlight off
// sticks even though the effect keeps running underneath, and it resumes the
// moment the backlight is switched back on.
std::atomic<bool> g_fourzone_off{false};

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

bool is_valid_hex_color(const std::string &hex) {
  if (hex.size() != 6)
    return false;

  for (char ch : hex) {
    if (!std::isxdigit(static_cast<unsigned char>(ch)))
      return false;
  }

  return true;
}

bool parse_hex_color(const std::string &hex, std::array<int, 3> *rgb) {
  if (!is_valid_hex_color(hex))
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

  // An explicit colour choice wins over a running animation, which would
  // otherwise repaint over it on the very next frame. Stopped before taking
  // g_fourzone_mutex: stopping joins the worker, which may be waiting for it.
  stop_keyboard_effect();

  std::string canonical_color = std::to_string(rgb_values[0]) + " " +
                                std::to_string(rgb_values[1]) + " " +
                                std::to_string(rgb_values[2]);

  if (omen_4zone_exists()) {
    std::string hex_val = rgb_triplet_to_hex(canonical_color);
    if (hex_val.empty())
      return "ERROR: Invalid RGB color";

    std::lock_guard<std::mutex> lock(g_fourzone_mutex);
    for (int zone = 0; zone < kFourZoneCount; zone++) {
      std::string result = write_rgb_zone_with_helper(zone, hex_val);
      if (result != "OK")
        return result;
    }

    // The keyboard is visibly lit again, whatever the switch said before.
    g_fourzone_off.store(false, std::memory_order_release);
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

  stop_keyboard_effect();

  std::string canonical_color = std::to_string(rgb_values[0]) + " " +
                                std::to_string(rgb_values[1]) + " " +
                                std::to_string(rgb_values[2]);

  if (omen_4zone_exists()) {
    std::string hex_val = rgb_triplet_to_hex(canonical_color);
    if (hex_val.empty())
      return "ERROR: Invalid RGB color";

    std::lock_guard<std::mutex> lock(g_fourzone_mutex);
    std::string result = write_rgb_zone_with_helper(zone, hex_val);
    if (result == "OK")
      g_fourzone_off.store(false, std::memory_order_release);
    return result;
  }

  return set_keyboard_color(canonical_color);
}

std::string get_keyboard_brightness() {
  if (omen_4zone_exists()) {
    // While switched off the zones are black by construction, and a running
    // animation is not painting them, so the flag is the truth. Otherwise
    // infer it from the colours, which also covers a board that boots dark.
    if (g_fourzone_off.load(std::memory_order_acquire))
      return "0";
    return fourzone_brightness_value();
  }

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

  // Four-zone Omen keyboards have no brightness sysfs; the LEDs are driven only
  // by per-zone RGB. Emulate the on/off toggle: brightness 0 stashes the
  // current colors and blacks every zone; a non-zero value restores them.
  if (omen_4zone_exists()) {
    // Serialize the whole four-zone path: it reads/writes g_fourzone_saved_colors
    // and issues per-zone hardware writes that must not interleave with a
    // concurrent toggle from another client thread.
    std::lock_guard<std::mutex> lock(g_fourzone_mutex);
    if (brightness_value == 0) {
      std::array<std::string, kFourZoneCount> stashed;
      for (int zone = 0; zone < kFourZoneCount; zone++) {
        std::string hex = read_text_file(fourzone_zone_path(zone));
        if (!is_valid_hex_color(hex))
          hex = "FFFFFF";
        stashed[zone] = hex;
      }
      // Only overwrite the saved colors if the keyboard is currently lit, so a
      // double "off" doesn't stash all-black and lose the real colors.
      bool any_lit = false;
      for (const auto &hex : stashed) {
        if (hex != "000000") {
          any_lit = true;
          break;
        }
      }
      if (any_lit)
        g_fourzone_saved_colors = stashed;

      // Flagged before the zones go dark so no animation frame lands after
      // them; the mutex keeps a frame already in flight ahead of this write.
      g_fourzone_off.store(true, std::memory_order_release);
      for (int zone = 0; zone < kFourZoneCount; zone++) {
        std::string result = write_rgb_zone_with_helper(zone, "000000");
        if (result != "OK")
          return result;
      }
      return "OK";
    }

    // Non-zero brightness: bring the stashed colours back, or light the zones
    // white when nothing was stashed so a board that booted dark visibly
    // switches on rather than staying black. A running animation resumes on
    // its next frame either way.
    g_fourzone_off.store(false, std::memory_order_release);
    std::array<std::string, kFourZoneCount> colors;
    colors.fill("FFFFFF");
    if (g_fourzone_saved_colors)
      colors = *g_fourzone_saved_colors;

    for (int zone = 0; zone < kFourZoneCount; zone++) {
      std::string result = write_rgb_zone_with_helper(zone, colors[zone]);
      if (result != "OK")
        return result;
    }
    return "OK";
  }

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

namespace {

constexpr const char *kRgbZonesBatchWriterPath = "/usr/bin/set-rgb-zones.sh";

bool write_text_file(const std::string &path, const std::string &value) {
  std::ofstream file(path);
  if (!file)
    return false;

  file << value;
  file.flush();
  return !file.fail();
}

// Whether the four zone files can be written directly. Checked on every
// frame: four access() calls cost microseconds, and the answer changes without
// a restart when the udev rule is applied after the service has started.
bool fourzone_direct_writes_available() {
  for (int zone = 0; zone < kFourZoneCount; zone++) {
    if (access(fourzone_zone_path(zone).c_str(), W_OK) != 0)
      return false;
  }
  return true;
}

std::string write_fourzone_frame(const std::array<std::string, kFourZoneCount> &hex) {
  // Fast path: no sudo, no fork, and no journal entry per frame.
  if (fourzone_direct_writes_available()) {
    for (int zone = 0; zone < kFourZoneCount; zone++) {
      if (!write_text_file(fourzone_zone_path(zone), hex[static_cast<size_t>(zone)]))
        return "ERROR: Failed to set zone color";
    }
    return "OK";
  }

  // Otherwise push all four zones through one helper invocation. Doing this
  // per-zone would mean four sudo execs for every frame of the animation.
  // Say so once: a service that stays on this path forks sudo for every
  // frame, which is worth a line in the journal explaining why.
  static std::once_flag fallback_notice;
  std::call_once(fallback_notice, [] {
    std::cerr << "four-zone lighting: the zone files are not writable by the "
                 "service, so animation frames go through the privileged helper "
                 "until the udev rule applies (re-run the installer or reboot)"
              << std::endl;
  });

  struct stat buffer;
  if (stat(kRgbZonesBatchWriterPath, &buffer) == 0) {
    int status = run_helper_command({kSudoPath, kRgbZonesBatchWriterPath, hex[0],
                                     hex[1], hex[2], hex[3]});
    if (status == 0)
      return "OK";
    return "ERROR: Failed to set zone color";
  }

  // Older install without the batch helper: fall back to the per-zone script.
  for (int zone = 0; zone < kFourZoneCount; zone++) {
    std::string result =
        write_rgb_zone_with_helper(zone, hex[static_cast<size_t>(zone)]);
    if (result != "OK")
      return result;
  }
  return "OK";
}

} // namespace

int keyboard_zone_count() { return omen_4zone_exists() ? kFourZoneCount : 1; }

std::array<int, 3> current_keyboard_rgb() {
  std::array<int, 3> rgb{255, 255, 255};

  if (omen_4zone_exists()) {
    std::string hex = read_text_file(kFourZoneZone0Path);
    if (parse_hex_color(hex, &rgb))
      return rgb;
    return {255, 255, 255};
  }

  if (parse_rgb_triplet(read_text_file(kSingleZoneColorPath), &rgb))
    return rgb;

  return {255, 255, 255};
}

std::string write_keyboard_colors_raw(const std::vector<std::string> &rgb_triplets) {
  if (rgb_triplets.empty())
    return "ERROR: No colors supplied";

  if (omen_4zone_exists()) {
    if (rgb_triplets.size() < static_cast<size_t>(kFourZoneCount))
      return "ERROR: Expected four zone colors";

    std::array<std::string, kFourZoneCount> hex;
    for (int zone = 0; zone < kFourZoneCount; zone++) {
      hex[static_cast<size_t>(zone)] =
          rgb_triplet_to_hex(rgb_triplets[static_cast<size_t>(zone)]);
      if (hex[static_cast<size_t>(zone)].empty())
        return "ERROR: Invalid RGB color";
    }

    // Serialised with the on/off toggle so a frame can neither interleave
    // with the zones being blacked out nor land after they were. While the
    // backlight is off the frame is simply dropped; the animation carries on
    // and paints again once it is switched on.
    std::lock_guard<std::mutex> lock(g_fourzone_mutex);
    if (g_fourzone_off.load(std::memory_order_acquire))
      return "OK";

    return write_fourzone_frame(hex);
  }

  std::array<int, 3> rgb;
  if (!parse_rgb_triplet(rgb_triplets.front(), &rgb))
    return "ERROR: Invalid RGB color";

  if (!write_text_file(kSingleZoneColorPath, std::to_string(rgb[0]) + " " +
                                                 std::to_string(rgb[1]) + " " +
                                                 std::to_string(rgb[2])))
    return "ERROR: Failed to write RGB color";

  return "OK";
}
