#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include <unistd.h>

#include "fan.hpp"

namespace {

bool expect(bool condition, const char *message) {
  if (condition)
    return true;

  std::cerr << "FAILED: " << message << std::endl;
  return false;
}

// A scratch directory standing in for /sys/devices/platform/hp-wmi/hwmon/hwmonN.
class FakeHwmon {
public:
  FakeHwmon() {
    char pattern[] = "/tmp/victus-fan-test-XXXXXX";
    char *dir = mkdtemp(pattern);
    path_ = dir ? dir : "";
  }

  ~FakeHwmon() {
    if (path_.empty())
      return;
    for (const char *name : {"fan1_target", "fan2_target", "pwm1_enable"})
      unlink((path_ + "/" + name).c_str());
    rmdir(path_.c_str());
  }

  const std::string &path() const { return path_; }

  void touch(const char *name) const {
    std::ofstream file(path_ + "/" + name);
    file << "0\n";
  }

private:
  std::string path_;
};

} // namespace

int main() {
  bool ok = true;

  // --- VICTUS_NO_FAN_CONTROL parsing -------------------------------------
  ok &= expect(!fan_control_disabled_by(nullptr),
               "an unset VICTUS_NO_FAN_CONTROL leaves fan control enabled");
  ok &= expect(!fan_control_disabled_by(""),
               "an empty VICTUS_NO_FAN_CONTROL leaves fan control enabled");
  ok &= expect(!fan_control_disabled_by("0"),
               "VICTUS_NO_FAN_CONTROL=0 leaves fan control enabled");
  ok &= expect(fan_control_disabled_by("1"),
               "VICTUS_NO_FAN_CONTROL=1 disables fan control");

  // --- fan target probe ----------------------------------------------------
  ok &= expect(fan_target_support_in("") == "UNSUPPORTED",
               "no hwmon directory means no fan targets");
  ok &= expect(fan_target_support_in("/nonexistent/victus-hwmon") == "UNSUPPORTED",
               "a missing hwmon directory means no fan targets");

  {
    FakeHwmon hwmon;
    ok &= expect(!hwmon.path().empty(), "scratch hwmon directory is created");
    hwmon.touch("pwm1_enable");
    ok &= expect(fan_target_support_in(hwmon.path()) == "UNSUPPORTED",
                 "pwm1_enable alone does not make targets supported");

    hwmon.touch("fan1_target");
    ok &= expect(fan_target_support_in(hwmon.path()) == "UNSUPPORTED",
                 "only one fan target file is not full support");

    hwmon.touch("fan2_target");
    ok &= expect(fan_target_support_in(hwmon.path()) == "SUPPORTED",
                 "both fan target files present means targets are supported");
  }

  // --- remembered fan profile ----------------------------------------------
  {
    auto max = parse_fan_state("MAX - -\n");
    ok &= expect(max && max->mode == "MAX" && !max->fan1_rpm && !max->fan2_rpm,
                 "a saved MAX profile round-trips with no speeds");

    auto manual = parse_fan_state("MANUAL 3600 3800");
    ok &= expect(manual && manual->mode == "MANUAL" && manual->fan1_rpm == 3600 &&
                     manual->fan2_rpm == 3800,
                 "a saved MANUAL profile keeps both speeds");

    auto partial = parse_fan_state("MANUAL 3600 -");
    ok &= expect(partial && partial->fan1_rpm == 3600 && !partial->fan2_rpm,
                 "a speed that was never set stays unset");

    auto bare = parse_fan_state("BETTER_AUTO");
    ok &= expect(bare && bare->mode == "BETTER_AUTO",
                 "the speeds are optional");

    auto damaged = parse_fan_state("MANUAL 36x0 -5");
    ok &= expect(damaged && !damaged->fan1_rpm && !damaged->fan2_rpm,
                 "damaged speeds are dropped without losing the mode");

    ok &= expect(!parse_fan_state(""), "an empty file is no profile");
    ok &= expect(!parse_fan_state("TURBO 1 2"), "an unknown mode is no profile");

    SavedFanState state;
    state.mode = "MANUAL";
    state.fan1_rpm = 4200;
    ok &= expect(format_fan_state(state) == "MANUAL 4200 -\n",
                 "the file is one line with - for an unset speed");
    auto again = parse_fan_state(format_fan_state(state));
    ok &= expect(again && again->mode == "MANUAL" && again->fan1_rpm == 4200 &&
                     !again->fan2_rpm,
                 "what is written is what is read back");
  }

  return ok ? 0 : 1;
}
