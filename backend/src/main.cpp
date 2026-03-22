#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "fan.hpp"
#include "keyboard.hpp"
#include "validation.hpp"

#define SOCKET_DIR "/run/victus-control"
#define SOCKET_PATH SOCKET_DIR "/victus_backend.sock"

namespace {

constexpr uint32_t kMaxCommandLength = 1024;

std::atomic<int> active_clients{0};
std::atomic<bool> server_running{true};
int g_server_socket = -1;

void on_client_connected() {
  int current = active_clients.fetch_add(1) + 1;
  std::cout << "Client connected (active: " << current << ")" << std::endl;
}

void on_client_disconnected() {
  int previous = active_clients.fetch_sub(1);
  int current = previous - 1;
  if (previous <= 0) {
    active_clients.store(0);
    current = 0;
  }
  std::cout << "Client disconnected (active: " << current << ")" << std::endl;

  if (current == 0) {
    auto result = ensure_better_auto_mode();
    if (result != "OK") {
      std::cerr
          << "Failed to enforce BETTER_AUTO mode after client disconnect: "
          << result << std::endl;
    }
  }
}

void signal_handler(int) {
  server_running.store(false, std::memory_order_release);
  if (g_server_socket >= 0) {
    close(g_server_socket);
    g_server_socket = -1;
  }
}

bool send_all(int socket, const void *buffer, size_t length) {
  const char *ptr = static_cast<const char *>(buffer);
  while (length > 0) {
    ssize_t bytes_sent = send(socket, ptr, length, 0);
    if (bytes_sent < 0) {
      if (errno == EINTR)
        continue;
      std::cerr << "Failed to send data: " << strerror(errno) << std::endl;
      return false;
    }
    if (bytes_sent == 0) {
      std::cerr << "Socket closed while sending data" << std::endl;
      return false;
    }
    ptr += bytes_sent;
    length -= static_cast<size_t>(bytes_sent);
  }
  return true;
}

bool read_all(int socket, void *buffer, size_t length) {
  char *ptr = static_cast<char *>(buffer);
  while (length > 0) {
    ssize_t bytes_read = read(socket, ptr, length);
    if (bytes_read < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (bytes_read == 0)
      return false;
    ptr += bytes_read;
    length -= static_cast<size_t>(bytes_read);
  }
  return true;
}

bool send_u32_le(int socket, uint32_t value) {
  unsigned char bytes[4] = {
      static_cast<unsigned char>(value & 0xFF),
      static_cast<unsigned char>((value >> 8) & 0xFF),
      static_cast<unsigned char>((value >> 16) & 0xFF),
      static_cast<unsigned char>((value >> 24) & 0xFF),
  };
  return send_all(socket, bytes, sizeof(bytes));
}

bool read_u32_le(int socket, uint32_t *value) {
  if (!value)
    return false;

  unsigned char bytes[4];
  if (!read_all(socket, bytes, sizeof(bytes)))
    return false;

  *value = static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
  return true;
}

std::string trim(const std::string &input) {
  size_t start = input.find_first_not_of(" \t\r\n");
  if (start == std::string::npos)
    return "";

  size_t end = input.find_last_not_of(" \t\r\n");
  return input.substr(start, end - start + 1);
}

bool has_extra_tokens(std::stringstream &ss) {
  std::string extra;
  return static_cast<bool>(ss >> extra);
}

void send_response(int client_socket, const std::string &response) {
  uint32_t len = static_cast<uint32_t>(response.size());
  if (!send_u32_le(client_socket, len))
    return;
  if (!send_all(client_socket, response.data(), len))
    return;
}

void handle_command(const std::string &command_str, int client_socket) {
  std::stringstream ss(command_str);
  std::string command;
  ss >> command;

  std::string response;

  if (command == "GET_FAN_SPEED") {
    std::string fan_num;
    ss >> fan_num;
    if (!fan_num.empty() && !has_extra_tokens(ss)) {
      response = get_fan_speed(fan_num);
    } else {
      response = "ERROR: Invalid GET_FAN_SPEED command format";
    }
  } else if (command == "GET_FAN_MAX_SPEED") {
    std::string fan_num;
    ss >> fan_num;
    if (!fan_num.empty() && !has_extra_tokens(ss)) {
      response = get_fan_max_speed(fan_num);
    } else {
      response = "ERROR: Invalid GET_FAN_MAX_SPEED command format";
    }
  } else if (command == "SET_FAN_SPEED") {
    std::string fan_num;
    std::string speed;
    ss >> fan_num >> speed;
    if (!fan_num.empty() && !speed.empty() && !has_extra_tokens(ss)) {
      response = set_fan_speed(fan_num, speed, true, true);
    } else {
      response = "ERROR: Invalid SET_FAN_SPEED command format";
    }
  } else if (command == "SET_FAN_MODE") {
    std::string remainder;
    std::getline(ss, remainder);
    remainder = trim(remainder);
    if (remainder.empty()) {
      response = "ERROR: Invalid SET_FAN_MODE command format";
    } else {
      std::string mode = normalize_mode(remainder);
      response = set_fan_mode(mode);
      if (response == "OK")
        fan_mode_trigger(mode);
    }
  } else if (command == "GET_FAN_MODE") {
    if (!has_extra_tokens(ss)) {
      response = get_fan_mode();
    } else {
      response = "ERROR: Invalid GET_FAN_MODE command format";
    }
  } else if (command == "GET_CPU_TEMP") {
    if (!has_extra_tokens(ss)) {
      response = get_cpu_temperature();
    } else {
      response = "ERROR: Invalid GET_CPU_TEMP command format";
    }
  } else if (command == "GET_KEYBOARD_COLOR") {
    if (!has_extra_tokens(ss)) {
      response = get_keyboard_color();
    } else {
      response = "ERROR: Invalid GET_KEYBOARD_COLOR command format";
    }
  } else if (command == "SET_KEYBOARD_COLOR") {
    std::string r, g, b;
    ss >> r >> g >> b;
    if (!r.empty() && !g.empty() && !b.empty() && !has_extra_tokens(ss)) {
      response = set_keyboard_color(r + " " + g + " " + b);
    } else {
      response = "ERROR: Invalid SET_KEYBOARD_COLOR command format";
    }
  } else if (command == "SET_KEYBOARD_ZONE_COLOR") {
    std::string zone_str, r, g, b;
    ss >> zone_str >> r >> g >> b;
    int zone = 0;
    if (!zone_str.empty() && !r.empty() && !g.empty() && !b.empty() &&
        !has_extra_tokens(ss) &&
        parse_bounded_int(zone_str, 0, 3, &zone)) {
      response = set_keyboard_zone_color(zone, r + " " + g + " " + b);
    } else {
      response = "ERROR: Invalid SET_KEYBOARD_ZONE_COLOR command format";
    }
  } else if (command == "GET_KEYBOARD_ZONE_COLOR") {
    std::string zone_str;
    int zone = 0;
    ss >> zone_str;
    if (!zone_str.empty() && !has_extra_tokens(ss) &&
        parse_bounded_int(zone_str, 0, 3, &zone)) {
      response = get_keyboard_zone_color(zone);
    } else {
      response = "ERROR: Invalid GET_KEYBOARD_ZONE_COLOR command format";
    }
  } else if (command == "GET_KEYBOARD_TYPE") {
    if (!has_extra_tokens(ss)) {
      response = get_keyboard_type();
    } else {
      response = "ERROR: Invalid GET_KEYBOARD_TYPE command format";
    }
  } else if (command == "GET_KBD_BRIGHTNESS") {
    if (!has_extra_tokens(ss)) {
      response = get_keyboard_brightness();
    } else {
      response = "ERROR: Invalid GET_KBD_BRIGHTNESS command format";
    }
  } else if (command == "SET_KBD_BRIGHTNESS") {
    std::string value;
    ss >> value;
    if (!value.empty() && !has_extra_tokens(ss)) {
      response = set_keyboard_brightness(value);
    } else {
      response = "ERROR: Invalid SET_KBD_BRIGHTNESS command format";
    }
  } else {
    response = "ERROR: Unknown command";
  }

  send_response(client_socket, response);
}

void handle_client(int client_socket) {
  on_client_connected();

  while (server_running.load(std::memory_order_acquire)) {
    uint32_t cmd_len = 0;
    if (!read_u32_le(client_socket, &cmd_len))
      break;

    if (cmd_len == 0 || cmd_len > kMaxCommandLength) {
      std::cerr << "Command too long or empty (" << cmd_len
                << " bytes). Closing connection.\n";
      break;
    }

    std::vector<char> buffer(cmd_len);
    if (!read_all(client_socket, buffer.data(), cmd_len))
      break;

    handle_command(std::string(buffer.begin(), buffer.end()), client_socket);
  }

  close(client_socket);
  on_client_disconnected();
}

} // namespace

int main() {
  struct sigaction sa = {};
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGINT, &sa, nullptr);

  int server_socket;
  struct sockaddr_un server_addr;

  unlink(SOCKET_PATH);

  server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
  if (server_socket < 0) {
    std::cerr << "Error creating socket: " << strerror(errno) << std::endl;
    return 1;
  }
  g_server_socket = server_socket;

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sun_family = AF_UNIX;
  strncpy(server_addr.sun_path, SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

  if (bind(server_socket, (struct sockaddr *)&server_addr,
           sizeof(server_addr)) < 0) {
    std::cerr << "Bind failed: " << strerror(errno) << std::endl;
    close(server_socket);
    g_server_socket = -1;
    return 1;
  }

  if (chmod(SOCKET_PATH, 0660) < 0) {
    std::cerr << "Failed to set socket permissions: " << strerror(errno)
              << std::endl;
    close(server_socket);
    g_server_socket = -1;
    return 1;
  }

  if (listen(server_socket, 5) < 0) {
    std::cerr << "Listen failed: " << strerror(errno) << std::endl;
    close(server_socket);
    g_server_socket = -1;
    return 1;
  }

  std::cout << "Server is listening..." << std::endl;

  auto ensure_result = ensure_better_auto_mode();
  if (ensure_result != "OK") {
    std::cerr << "Failed to enforce initial BETTER_AUTO mode: " << ensure_result
              << std::endl;
  }

  while (server_running.load(std::memory_order_acquire)) {
    int client_socket = accept(server_socket, nullptr, nullptr);
    if (client_socket < 0) {
      if (!server_running.load(std::memory_order_acquire))
        break;
      if (errno == EINTR)
        continue;
      perror("accept");
      continue;
    }

    std::thread(handle_client, client_socket).detach();
  }

  if (g_server_socket >= 0) {
    close(g_server_socket);
    g_server_socket = -1;
  }
  shutdown_fan_controller();
  unlink(SOCKET_PATH);
  std::cout << "Server shut down." << std::endl;
  return 0;
}
