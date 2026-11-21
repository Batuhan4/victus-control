#pragma once

#include <optional>
#include <string>

std::string set_fan_mode(const std::string &value);
std::string get_fan_mode();

std::string get_fan_speed(const std::string &fan_num);
std::string get_fan_max_speed(const std::string &fan_num);
std::string set_fan_speed(const std::string &fan_num, const std::string &speed);
std::string get_manual_fan_target();
std::string get_cpu_temperature();
std::string get_gpu_temperature();
std::string get_gpu_usage();
std::string get_gpu_vram();
// "DISABLED" while VICTUS_NO_FAN_CONTROL=1 is set for the service,
// "SUPPORTED" when the driver exposes fan1_target and fan2_target for this
// board, otherwise "UNSUPPORTED". Boards whose BIOS refuses software fan
// control never get those files, so MANUAL and Better Auto (which drives the
// fans through the same targets) cannot work there; only AUTO and MAX can.
// The UI uses this to offer just the profiles that do something.
std::string get_fan_target_support();
// The rule behind get_fan_target_support(), split out so it can be tested
// against a scratch directory: "SUPPORTED" when both target files exist.
std::string fan_target_support_in(const std::string &hwmon_dir);

// True when VICTUS_NO_FAN_CONTROL=1 is set: the backend then leaves the fans
// to the firmware and every fan-changing command is refused.
bool fan_control_disabled();
// The rule behind fan_control_disabled(), split out so it can be tested.
bool fan_control_disabled_by(const char *value);

// Puts the driver back into AUTO so the firmware curve runs the fans, after
// stopping Better Auto and any MANUAL/MAX watchdog. Used at start-up whenever
// the backend is not going to run the fans itself, so nothing left behind by
// a previous run (a pinned manual target, MAX) stays in effect.
std::string restore_firmware_fan_control();

// The profile remembered across restarts: the mode, plus the pinned speeds
// when that mode is MANUAL.
struct SavedFanState {
    std::string mode;
    std::optional<int> fan1_rpm;
    std::optional<int> fan2_rpm;
};
// The one-line file format, split out so it can be tested.
std::string format_fan_state(const SavedFanState &state);
std::optional<SavedFanState> parse_fan_state(const std::string &text);

// Re-applies the profile the user last chose (saved under $STATE_DIRECTORY).
// Returns true if a saved profile was applied or firmware AUTO was chosen
// after a failed MANUAL restore. Otherwise the caller picks a default.
bool restore_saved_fan_mode();

std::string ensure_better_auto_mode();
void shutdown_fan_controller();
