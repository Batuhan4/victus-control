#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "effects.hpp"
#include "keyboard.hpp"
#include "validation.hpp"

namespace {

constexpr int kMinSpeed = 1;
constexpr int kMaxSpeed = 100;
constexpr int kDefaultSpeed = 50;

// A single-zone frame is one ~0.15ms sysfs write, so 30fps is nearly free.
// A four-zone frame is one batched helper exec, which is far dearer, so it runs
// slower; 12fps still reads as smooth motion across four wide zones.
constexpr int kSingleZoneFrameMs = 33;
constexpr int kFourZoneFrameMs = 80;

// Seconds for one full cycle at the slowest and fastest speed settings.
constexpr double kSlowestPeriodSec = 24.0;
constexpr double kFastestPeriodSec = 1.2;

// The keyboard is checked for being switched off this often. Repainting a dark
// keyboard would be wasted EC traffic, so frames are skipped while it is off.
constexpr auto kBrightnessPollInterval = std::chrono::milliseconds(500);

// Zone indices in left-to-right order: far left, WASD, middle, right.
// The wave is offset along this path, so the colour travels across the board
// instead of the zones cycling in an arbitrary order.
constexpr std::array<int, 4> kFlowOrder = {2, 3, 1, 0};

enum class Effect { Static, Rainbow, Breathe, Flow };

std::mutex g_lifecycle_mutex; // guards g_thread across start/stop
std::thread g_thread;

std::atomic<bool> g_running{false};
std::atomic<int> g_effect{static_cast<int>(Effect::Static)};
std::atomic<int> g_speed{kDefaultSpeed};

// Lets stop_keyboard_effect() wake the sleeping worker immediately rather than
// waiting out a frame interval.
std::mutex g_sleep_mutex;
std::condition_variable g_sleep_cv;

// Colour BREATHE pulses, sampled when the effect starts.
std::mutex g_base_mutex;
std::array<int, 3> g_base_rgb{255, 255, 255};

// systemd sets STATE_DIRECTORY from StateDirectory= in the unit. Without it
// there is nowhere durable to write, and persistence is simply skipped.
std::string state_file_path() {
  const char *dir = std::getenv("STATE_DIRECTORY");
  if (dir == nullptr || *dir == '\0')
    return "";
  return std::string(dir) + "/effect.conf";
}

const char *effect_name(Effect effect) {
  switch (effect) {
  case Effect::Rainbow:
    return "RAINBOW";
  case Effect::Breathe:
    return "BREATHE";
  case Effect::Flow:
    return "FLOW";
  case Effect::Static:
    break;
  }
  return "STATIC";
}

bool effect_from_name(const std::string &name, Effect *out) {
  if (name == "STATIC" || name == "NONE" || name == "OFF")
    *out = Effect::Static;
  else if (name == "RAINBOW")
    *out = Effect::Rainbow;
  else if (name == "BREATHE")
    *out = Effect::Breathe;
  else if (name == "FLOW" || name == "WAVE" || name == "RIVER")
    *out = Effect::Flow;
  else
    return false;

  return true;
}

// hue in [0,1), saturation and value in [0,1].
std::array<int, 3> hsv_to_rgb(double hue, double saturation, double value) {
  hue -= std::floor(hue);

  double sector = hue * 6.0;
  int index = static_cast<int>(sector) % 6;
  double fraction = sector - std::floor(sector);

  double p = value * (1.0 - saturation);
  double q = value * (1.0 - saturation * fraction);
  double t = value * (1.0 - saturation * (1.0 - fraction));

  double r = 0.0, g = 0.0, b = 0.0;
  switch (index) {
  case 0: r = value; g = t;     b = p;     break;
  case 1: r = q;     g = value; b = p;     break;
  case 2: r = p;     g = value; b = t;     break;
  case 3: r = p;     g = q;     b = value; break;
  case 4: r = t;     g = p;     b = value; break;
  default: r = value; g = p;    b = q;     break;
  }

  auto to_byte = [](double channel) {
    int scaled = static_cast<int>(std::lround(channel * 255.0));
    return std::clamp(scaled, 0, 255);
  };

  return {to_byte(r), to_byte(g), to_byte(b)};
}

std::string rgb_to_triplet(const std::array<int, 3> &rgb) {
  return std::to_string(rgb[0]) + " " + std::to_string(rgb[1]) + " " +
         std::to_string(rgb[2]);
}

// Maps speed 1-100 onto a cycle period, so the phase advance per second is
// 1/period. Interpolated on the period rather than the rate, which keeps the
// slider feeling even across its travel.
double phase_step_per_second(int speed) {
  double t = (std::clamp(speed, kMinSpeed, kMaxSpeed) - kMinSpeed) /
             static_cast<double>(kMaxSpeed - kMinSpeed);
  double period = kSlowestPeriodSec + t * (kFastestPeriodSec - kSlowestPeriodSec);
  return 1.0 / period;
}

bool keyboard_is_off() {
  std::string brightness = get_keyboard_brightness();
  return brightness == "0";
}

std::vector<std::string> frame_colors(Effect effect, double phase,
                                      int zone_count) {
  std::vector<std::string> colors(static_cast<size_t>(zone_count));

  if (effect == Effect::Breathe) {
    std::array<int, 3> base;
    {
      std::lock_guard<std::mutex> lock(g_base_mutex);
      base = g_base_rgb;
    }
    // Never fades fully to black: at the bottom of the pulse the keyboard
    // should still be visibly lit rather than appearing switched off.
    double level = 0.15 + 0.85 * (0.5 - 0.5 * std::cos(2.0 * M_PI * phase));
    std::array<int, 3> rgb = {
        std::clamp(static_cast<int>(std::lround(base[0] * level)), 0, 255),
        std::clamp(static_cast<int>(std::lround(base[1] * level)), 0, 255),
        std::clamp(static_cast<int>(std::lround(base[2] * level)), 0, 255)};
    std::fill(colors.begin(), colors.end(), rgb_to_triplet(rgb));
    return colors;
  }

  if (effect == Effect::Flow && zone_count == 4) {
    // Spread a quarter of the hue wheel across the zones, ordered along the
    // keyboard, so the colour appears to flow from one side to the other.
    for (size_t position = 0; position < kFlowOrder.size(); position++) {
      double zone_phase = phase + static_cast<double>(position) / kFlowOrder.size();
      colors[static_cast<size_t>(kFlowOrder[position])] =
          rgb_to_triplet(hsv_to_rgb(zone_phase, 1.0, 1.0));
    }
    return colors;
  }

  // RAINBOW, and FLOW on single-zone hardware where there is nowhere to flow.
  std::string color = rgb_to_triplet(hsv_to_rgb(phase, 1.0, 1.0));
  std::fill(colors.begin(), colors.end(), color);
  return colors;
}

void effect_loop() {
  int zone_count = keyboard_zone_count();
  auto frame_interval = std::chrono::milliseconds(
      zone_count > 1 ? kFourZoneFrameMs : kSingleZoneFrameMs);

  double phase = 0.0;
  auto last_frame = std::chrono::steady_clock::now();
  auto last_brightness_check = last_frame - kBrightnessPollInterval;
  bool paused = false;

  while (g_running.load(std::memory_order_acquire)) {
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - last_frame).count();
    last_frame = now;

    // Advance on measured elapsed time, so the animation runs at the same speed
    // regardless of how long a hardware write actually took.
    phase += elapsed * phase_step_per_second(g_speed.load(std::memory_order_relaxed));
    phase -= std::floor(phase);

    if (now - last_brightness_check >= kBrightnessPollInterval) {
      paused = keyboard_is_off();
      last_brightness_check = now;
    }

    if (!paused) {
      Effect effect = static_cast<Effect>(g_effect.load(std::memory_order_relaxed));
      write_keyboard_colors_raw(frame_colors(effect, phase, zone_count));
    }

    std::unique_lock<std::mutex> lock(g_sleep_mutex);
    g_sleep_cv.wait_for(lock, frame_interval, [] {
      return !g_running.load(std::memory_order_acquire);
    });
  }
}

// Signals the worker and joins it. The caller must hold g_lifecycle_mutex.
void stop_locked() {
  if (!g_thread.joinable())
    return;

  g_running.store(false, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lock(g_sleep_mutex);
  }
  g_sleep_cv.notify_all();
  g_thread.join();
  g_effect.store(static_cast<int>(Effect::Static), std::memory_order_relaxed);
}

void save_effect_state(Effect effect, int speed) {
  std::string path = state_file_path();
  if (path.empty())
    return;

  std::ofstream file(path, std::ios::trunc);
  if (!file)
    return;

  file << effect_name(effect) << " " << speed << "\n";
}

} // namespace

std::string set_keyboard_effect(const std::string &name,
                                const std::string &speed) {
  Effect effect;
  if (!effect_from_name(normalize_mode(name), &effect))
    return "ERROR: Unknown keyboard effect";

  // Omitting the speed keeps the rate already in use rather than snapping the
  // animation back to the default.
  int speed_value = g_speed.load(std::memory_order_relaxed);
  if (!speed.empty() &&
      !parse_bounded_int(speed, kMinSpeed, kMaxSpeed, &speed_value))
    return "ERROR: Invalid effect speed";

  std::lock_guard<std::mutex> lock(g_lifecycle_mutex);

  g_speed.store(speed_value, std::memory_order_relaxed);

  if (effect == Effect::Static) {
    stop_locked();
    save_effect_state(Effect::Static, speed_value);
    return "OK";
  }

  if (effect == Effect::Breathe) {
    std::lock_guard<std::mutex> base_lock(g_base_mutex);
    g_base_rgb = current_keyboard_rgb();
    // A black keyboard has nothing to pulse; fall back to white so the effect
    // is visible instead of silently doing nothing.
    if (g_base_rgb[0] == 0 && g_base_rgb[1] == 0 && g_base_rgb[2] == 0)
      g_base_rgb = {255, 255, 255};
  }

  // Already animating: swap the effect in place so the phase keeps running and
  // the transition does not stutter.
  if (g_thread.joinable()) {
    g_effect.store(static_cast<int>(effect), std::memory_order_relaxed);
    save_effect_state(effect, speed_value);
    return "OK";
  }

  g_effect.store(static_cast<int>(effect), std::memory_order_relaxed);
  g_running.store(true, std::memory_order_release);
  g_thread = std::thread(effect_loop);
  save_effect_state(effect, speed_value);
  return "OK";
}

std::string get_keyboard_effect() {
  std::lock_guard<std::mutex> lock(g_lifecycle_mutex);
  Effect effect = g_thread.joinable()
                      ? static_cast<Effect>(g_effect.load(std::memory_order_relaxed))
                      : Effect::Static;
  return std::string(effect_name(effect)) + " " +
         std::to_string(g_speed.load(std::memory_order_relaxed));
}

void stop_keyboard_effect() {
  std::lock_guard<std::mutex> lock(g_lifecycle_mutex);
  stop_locked();
}

void restore_keyboard_effect() {
  std::string path = state_file_path();
  if (path.empty())
    return;

  std::ifstream file(path);
  if (!file)
    return;

  std::string name;
  int speed = kDefaultSpeed;
  file >> name >> speed;
  if (name.empty() || name == "STATIC")
    return;

  std::string result = set_keyboard_effect(name, std::to_string(speed));
  if (result != "OK")
    return;
}
