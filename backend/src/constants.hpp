#pragma once

// Fan control constants
const int FAN_REAPPLY_SECONDS = 100;

// Protocol constants
const uint32_t MAX_COMMAND_LENGTH = 4096;
const uint32_t BASIC_COMMAND_LIMIT = 1024;

// Fan speed limits
const int MIN_FAN_RPM = 0;
const int MAX_FAN_RPM = 6000;

// Keyboard brightness limits
const int MIN_BRIGHTNESS = 0;
const int MAX_BRIGHTNESS = 3;