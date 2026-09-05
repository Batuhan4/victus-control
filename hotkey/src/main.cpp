// victus-hotkeyd -- opens Victus Control when the dedicated OMEN key is pressed.
//
// The hp-wmi driver reports the OMEN key on its "HP WMI hotkeys" input device.
// On this keymap two scancodes, 0x21a5 and 0x21a8, both resolve to KEY_PROG2,
// so the daemon matches on the keycode rather than on either scancode.
//
// Nothing here talks to the backend: it re-runs the GUI binary, which is a
// single instance, so the second press raises the window that is already open.

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <iostream>
#include <linux/input.h>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {

constexpr char kDeviceName[] = "HP WMI hotkeys";
constexpr char kAppBinary[] = "/usr/bin/victus-control";
constexpr int kOmenKey = KEY_PROG2;

// The firmware sometimes reports one physical press twice; collapse those so a
// single press does not race two launches of the GUI against each other.
constexpr auto kDebounce = std::chrono::milliseconds(400);

// How long to wait before re-scanning when the input device is absent, which
// happens while hp_wmi is being reloaded (an install, or a DKMS rebuild).
constexpr auto kRescanDelay = std::chrono::seconds(5);

std::atomic<bool> g_running{true};

void on_signal(int)
{
  g_running = false;
}

// Locates the hotkey device by name. The event node number is not stable across
// boots or module reloads, so it must not be hardcoded.
int open_hotkey_device()
{
  DIR *dir = opendir("/dev/input");
  if (dir == nullptr)
    return -1;

  int found = -1;
  while (dirent *entry = readdir(dir)) {
    if (std::strncmp(entry->d_name, "event", 5) != 0)
      continue;

    const std::string path = std::string("/dev/input/") + entry->d_name;
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
      continue;

    char name[256] = {};
    if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0 &&
        std::strcmp(name, kDeviceName) == 0) {
      found = fd;
      break;
    }

    close(fd);
  }

  closedir(dir);
  return found;
}

// Double-forks so the GUI is reparented to init: the daemon never waits on it
// and never accumulates zombies, however many times the key is pressed.
void launch_app()
{
  const pid_t intermediate = fork();
  if (intermediate < 0) {
    std::cerr << "victus-hotkeyd: fork failed: " << std::strerror(errno) << std::endl;
    return;
  }

  if (intermediate == 0) {
    if (fork() == 0) {
      execl(kAppBinary, kAppBinary, static_cast<char *>(nullptr));
      _exit(127);
    }
    _exit(0);
  }

  waitpid(intermediate, nullptr, 0);
}

// Reads key events until the device goes away or a signal arrives.
void watch_device(int fd, std::chrono::steady_clock::time_point &last_press)
{
  while (g_running) {
    pollfd pfd{fd, POLLIN, 0};
    const int ready = poll(&pfd, 1, 1000);

    if (ready == 0)
      continue;
    if (ready < 0) {
      if (errno == EINTR)
        continue;
      return;
    }

    input_event event{};
    if (read(fd, &event, sizeof(event)) != static_cast<ssize_t>(sizeof(event)))
      return;  // the device was unplugged or the module was reloaded

    if (event.type != EV_KEY || event.code != kOmenKey || event.value != 1)
      continue;

    const auto now = std::chrono::steady_clock::now();
    if (now - last_press < kDebounce)
      continue;
    last_press = now;

    launch_app();
  }
}

}  // namespace

int main()
{
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  // Start outside the debounce window so the very first press is never eaten.
  auto last_press = std::chrono::steady_clock::now() - kDebounce;
  bool announced = false;

  while (g_running) {
    const int fd = open_hotkey_device();
    if (fd < 0) {
      if (announced) {
        std::cerr << "victus-hotkeyd: \"" << kDeviceName
                  << "\" went away, waiting for it to come back" << std::endl;
        announced = false;
      }
      std::this_thread::sleep_for(kRescanDelay);
      continue;
    }

    if (!announced) {
      std::cerr << "victus-hotkeyd: watching \"" << kDeviceName
                << "\" for the OMEN key" << std::endl;
      announced = true;
    }

    watch_device(fd, last_press);
    close(fd);
  }

  return 0;
}
