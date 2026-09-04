#pragma once

#include <string>

void fan_mode_trigger(const std::string mode);
std::string set_fan_mode(const std::string &value);
std::string get_fan_mode();

std::string get_fan_speed(const std::string &fan_num);
std::string get_fan_max_speed(const std::string &fan_num);
std::string set_fan_speed(const std::string &fan_num, const std::string &speed, bool trigger_mode = true, bool update_cache = true);
std::string get_cpu_temperature();
std::string get_gpu_temperature();
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

std::string ensure_better_auto_mode();
void shutdown_fan_controller();
