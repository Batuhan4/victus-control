#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <chrono>
#include <array>

namespace Config {
    // Socket configuration
    constexpr auto SOCKET_DIR = "/run/victus-control";
    constexpr auto SOCKET_PATH = "/run/victus-control/victus_backend.sock";
    constexpr size_t MAX_COMMAND_SIZE = 1024;
    constexpr size_t MAX_RESPONSE_SIZE = 4096;
    
    // Fan control constants
    constexpr int MIN_RPM = 2000;
    constexpr int FAN1_MAX_RPM = 5800;
    constexpr int FAN2_MAX_RPM = 6100;
    constexpr int RPM_STEPS = 8;
    constexpr int MAX_ALLOWED_RPM = 10000;  // Safety limit
    
    // Better auto mode parameters
    constexpr int BETTER_AUTO_MIN_RPM = 2000;
    constexpr std::array<int, 2> BETTER_AUTO_MAX_FALLBACK = {5800, 6100};
    constexpr int BETTER_AUTO_STEPS = 8;
    constexpr auto BETTER_AUTO_TICK = std::chrono::seconds(2);
    constexpr auto BETTER_AUTO_REAPPLY = std::chrono::seconds(90);
    constexpr auto FAN_APPLY_GAP = std::chrono::seconds(10);
    
    // Temperature thresholds for better auto mode
    constexpr std::array<double, 7> TEMP_THRESHOLDS = {50.0, 60.0, 70.0, 75.0, 80.0, 85.0, 90.0};
    constexpr std::array<double, 7> USAGE_THRESHOLDS = {20.0, 35.0, 50.0, 65.0, 75.0, 85.0, 95.0};
    
    // Update intervals
    constexpr auto FAN_MODE_REFRESH_INTERVAL = std::chrono::seconds(80);
    constexpr auto FAN_SPEED_UPDATE_INTERVAL = std::chrono::seconds(2);
    
    // RGB color bounds
    constexpr int MIN_RGB_VALUE = 0;
    constexpr int MAX_RGB_VALUE = 255;
    
    // Keyboard brightness bounds
    constexpr int MIN_BRIGHTNESS = 0;
    constexpr int MAX_BRIGHTNESS = 255;
}

#endif // CONFIG_HPP