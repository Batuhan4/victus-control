#include "fan.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
static fs::path board;
static int rejected_fan = 0;

std::string find_hwmon_directory(const std::string &)
{
    return board.string();
}

// The real controller forks before it invokes the RPM helper. Model the
// helper's PWM and target writes on a temporary board instead of using sudo.
extern "C" int __real_execv(const char *, char *const []);
extern "C" int __wrap_execv(const char *path, char *const args[])
{
    if (std::strcmp(path, "/usr/bin/sudo") != 0 ||
        std::strcmp(args[1], "/usr/bin/set-fan-speed.sh") != 0) {
        return __real_execv(path, args);
    }

    int mode = open((board / "pwm1_enable").c_str(), O_WRONLY | O_TRUNC);
    if (mode < 0 || write(mode, "1", 1) != 1) _exit(1);
    close(mode);
    if (args[2][0] - '0' == rejected_fan) _exit(1);

    int target = open((board / ("fan" + std::string(args[2]) + "_target")).c_str(),
                      O_WRONLY | O_TRUNC);
    size_t length = std::strlen(args[3]);
    if (target < 0 || write(target, args[3], length) != static_cast<ssize_t>(length)) _exit(1);
    close(target);
    _exit(0);
}

static std::string read(const fs::path &path)
{
    std::ifstream stream(path);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

// Polls for up to 15 s, enough for the 10 s inter-fan gap.
template <typename Condition>
static bool wait_for(Condition condition)
{
    for (int i = 0; i < 300; ++i) {
        if (condition()) return true;
        usleep(50'000);
    }
    return condition();
}

static bool expect(bool condition, const char *message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    char directory[] = "/tmp/victus-fan-restore-XXXXXX";
    if (!mkdtemp(directory)) return 1;
    fs::path root(directory);
    board = root / "hwmon0";
    fs::create_directory(board);
    setenv("STATE_DIRECTORY", directory, 1);
    unsetenv("VICTUS_NO_FAN_CONTROL");
    auto profile = root / "fan.conf";

    std::ofstream(board / "pwm1_enable") << "2";
    std::ofstream(board / "fan1_target") << "1700";
    std::ofstream(board / "fan2_target") << "1700";

    std::string scenario = argv[1];
    bool ok = true;
    if (scenario == "manual") {
        std::ofstream(profile) << "MANUAL 0 4600\n";
        auto started = std::chrono::steady_clock::now();
        ok &= expect(restore_saved_fan_mode(), "valid manual profile should be restored");
        ok &= expect(std::chrono::steady_clock::now() - started < std::chrono::seconds(3),
                     "restore must not hold startup for the inter-fan gap");
        ok &= expect(get_fan_mode() == "MANUAL", "manual profile should be active");
        ok &= expect(read(board / "fan1_target") == "0", "zero RPM should survive restore");
        ok &= expect(get_manual_fan_target() == "0", "UI should receive the applied target");
        ok &= expect(wait_for([&] { return read(board / "fan2_target") == "4600"; }),
                     "fan 2 target should follow in the background");
        ok &= expect(get_fan_mode() == "MANUAL", "manual profile should stay active");
    } else if (scenario == "restore-mode-switch") {
        std::ofstream(profile) << "MANUAL 4300 4600\n";
        ok &= expect(restore_saved_fan_mode(), "valid manual profile should be restored");
        ok &= expect(set_fan_mode("MAX") == "OK", "a client may switch mode while fan 2 is pending");
        usleep(11'500'000);
        ok &= expect(get_fan_mode() == "MAX", "the pending restore must not switch MAX back to manual");
        ok &= expect(read(board / "fan2_target") == "1700", "the pending fan 2 target should be dropped");
        ok &= expect(read(profile) == "MAX - -\n", "the client's choice should be saved");
    } else if (scenario == "restore-client-speed") {
        std::ofstream(profile) << "MANUAL 4300 4600\n";
        ok &= expect(restore_saved_fan_mode(), "valid manual profile should be restored");
        ok &= expect(set_fan_speed("2", "3000") == "OK", "a client may set fan 2 while the restore waits");
        usleep(1'000'000);
        ok &= expect(read(board / "fan2_target") == "3000", "the restore must not overwrite the client's fan 2");
        ok &= expect(get_fan_mode() == "MANUAL", "manual should stay active");
        ok &= expect(read(profile) == "MANUAL 4300 3000\n", "the client's speed should be saved");
    } else if (scenario == "incomplete" || scenario == "rejected") {
        const char *stored = scenario == "rejected" ? "MANUAL 4300 4600\n" : "MANUAL - 4600\n";
        std::ofstream(profile) << stored;
        if (scenario == "rejected") rejected_fan = 2;
        ok &= expect(restore_saved_fan_mode(), "startup should handle the manual restore failure");
        ok &= expect(wait_for([] { return get_fan_mode() == "AUTO"; }),
                     "firmware AUTO should run after a failed manual restore");
        ok &= expect(read(board / "fan2_target") == "1700", "rejected fan target should remain untouched");
        ok &= expect(read(profile) == stored, "fallback should preserve the saved file");
    } else if (scenario == "save") {
        std::ofstream(profile) << "AUTO - -\n";
        ok &= expect(set_fan_mode("MANUAL") == "OK", "test board should enter manual");
        rejected_fan = 2;
        ok &= expect(set_fan_speed("2", "4900") != "OK", "fan 2 should reject the RPM");
        rejected_fan = 0;
        ok &= expect(set_fan_speed("1", "4300") == "OK", "fan 1 should accept the RPM");
        ok &= expect(read(profile) == "AUTO - -\n", "one applied RPM must not replace a usable profile");
        ok &= expect(set_fan_speed("2", "4600") == "OK", "fan 2 should accept the corrected RPM");
        ok &= expect(read(profile) == "MANUAL 4300 4600\n", "both applied targets should be saved");
    } else if (scenario == "mode-switch") {
        ok &= expect(set_fan_mode("MANUAL") == "OK", "test board should enter manual");
        ok &= expect(set_fan_speed("1", "4300") == "OK", "fan 1 should accept the RPM");
        auto waiting = std::async(std::launch::async, [] { return set_fan_speed("2", "4600"); });
        ok &= expect(waiting.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout,
                     "fan 2 should observe the inter-fan delay");
        ok &= expect(set_fan_mode("MAX") == "OK", "test board should switch to MAX");
        ok &= expect(waiting.get() != "OK", "delayed manual write should be cancelled");
        ok &= expect(get_fan_mode() == "MAX", "delayed helper must not switch MAX back to manual");
        ok &= expect(read(board / "fan2_target") == "1700", "cancelled fan target should stay untouched");
        ok &= expect(read(profile) == "MAX - -\n", "cancelled write must not replace the saved profile");
    } else if (scenario == "default") {
        std::ofstream(profile) << "INVALID\n";
        ok &= expect(!restore_saved_fan_mode(), "invalid profile should let startup pick a default");
        ok &= expect(ensure_better_auto_mode() == "OK", "Better Auto should start on the test board");
        for (int i = 0; i < 100 && read(board / "fan1_target") == "1700"; ++i) usleep(20'000);
        ok &= expect(read(board / "fan1_target") != "1700", "Better Auto should write a fan target");
        ok &= expect(read(profile) == "INVALID\n", "Better Auto should not persist internal RPM writes");
    } else {
        ok = false;
    }

    shutdown_fan_controller();
    fs::remove_all(root);
    return ok ? 0 : 1;
}
