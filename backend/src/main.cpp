#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

#include "fan.hpp"
#include "keyboard.hpp"

#define SOCKET_DIR "/run/victus-control"
#define SOCKET_PATH SOCKET_DIR "/victus_backend.sock"

static std::atomic<int> active_clients{0};

static void on_client_connected() {
  int current = active_clients.fetch_add(1) + 1;
  std::cout << "Client connected (active: " << current << ")" << std::endl;
}

static void on_client_disconnected() {
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

// Helper function to reliably send a block of data
bool send_all(int socket, const void *buffer, size_t length) {
  const char *ptr = static_cast<const char *>(buffer);
  while (length > 0) {
    ssize_t bytes_sent = send(socket, ptr, length, 0);
    if (bytes_sent < 1) {
      std::cerr << "Failed to send data" << std::endl;
      return false;
    }
    ptr += bytes_sent;
    length -= bytes_sent;
  }
  return true;
}

// Helper function to reliably read a block of data
bool read_all(int socket, void *buffer, size_t length) {
  char *ptr = static_cast<char *>(buffer);
  while (length > 0) {
    ssize_t bytes_read = read(socket, ptr, length);
    if (bytes_read < 1) {
      // Client disconnected or error
      return false;
    }
    ptr += bytes_read;
    length -= bytes_read;
  }
  return true;
}

static std::string trim(const std::string &input) {
  size_t start = input.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  size_t end = input.find_last_not_of(" \t\r\n");
  return input.substr(start, end - start + 1);
}

static std::string normalize_mode(std::string mode) {
  for (char &ch : mode) {
    if (ch == '-' || ch == ' ') {
      ch = '_';
    } else {
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
  }
  return mode;
}

void handle_command(const std::string &command_str, int client_socket) {
  std::stringstream ss(command_str);
  std::string command;
  ss >> command;

  std::string response;

  if (command == "GET_FAN_SPEED") {
    std::string fan_num;
    ss >> fan_num;
    response = get_fan_speed(fan_num);
  } else if (command == "SET_FAN_SPEED") {
    std::string fan_num;
    std::string speed;
    ss >> fan_num >> speed;
    if (!fan_num.empty() && !speed.empty()) {
      response =
          set_fan_speed(fan_num, speed, true,
                        true); // true = allow triggering fan_mode_trigger
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
      if (response == "OK") {
        fan_mode_trigger(mode);
      }
    }
  } else if (command == "GET_FAN_MODE") {
    response = get_fan_mode();
  } else if (command == "GET_KEYBOARD_COLOR") {
    response = get_keyboard_color();
  } else if (command == "SET_KEYBOARD_COLOR") {
    std::string r, g, b;
    ss >> r >> g >> b;
    if (!r.empty() && !g.empty() && !b.empty()) {
      response = set_keyboard_color(r + " " + g + " " + b);
    } else {
      response = "ERROR: Invalid SET_KEYBOARD_COLOR command format";
    }
  } else if (command == "SET_KEYBOARD_ZONE_COLOR") {
    std::string zone_str, r, g, b;
    ss >> zone_str >> r >> g >> b;
    if (!zone_str.empty() && !r.empty() && !g.empty() && !b.empty()) {
      try {
        int zone = std::stoi(zone_str);
        response = set_keyboard_zone_color(zone, r + " " + g + " " + b);
      } catch (...) {
        response = "ERROR: Invalid zone value";
      }
    } else {
      response = "ERROR: Invalid SET_KEYBOARD_ZONE_COLOR command format";
    }
  } else if (command == "GET_KEYBOARD_ZONE_COLOR") {
    std::string zone_str;
    ss >> zone_str;
    if (!zone_str.empty()) {
      try {
        int zone = std::stoi(zone_str);
        response = get_keyboard_zone_color(zone);
      } catch (...) {
        response = "ERROR: Invalid zone value";
      }
    } else {
      response = "ERROR: Invalid GET_KEYBOARD_ZONE_COLOR command format";
    }
  } else if (command == "GET_KEYBOARD_TYPE") {
    response = get_keyboard_type();
  } else if (command == "GET_KBD_BRIGHTNESS") {
    response = get_keyboard_brightness();
  } else if (command == "SET_KBD_BRIGHTNESS") {
    std::string value;
    ss >> value;
    response = set_keyboard_brightness(value);
  } else
    response = "ERROR: Unknown command";

  uint32_t len = response.length();
  if (!send_all(client_socket, &len, sizeof(len)))
    return;
  if (!send_all(client_socket, response.c_str(), len))
    return;
}

int main() {
  int server_socket, client_socket;
  struct sockaddr_un server_addr;

  unlink(SOCKET_PATH);

  server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
  if (server_socket < 0) {
    std::cerr << "Error creating socket: " << strerror(errno) << std::endl;
    return 1;
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sun_family = AF_UNIX;
  strncpy(server_addr.sun_path, SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

  if (bind(server_socket, (struct sockaddr *)&server_addr,
           sizeof(server_addr)) < 0) {
    std::cerr << "Bind failed: " << strerror(errno) << std::endl;
    close(server_socket);
    return 1;
  }

  if (chmod(SOCKET_PATH, 0660) < 0) {
    std::cerr << "Failed to set socket permissions: " << strerror(errno)
              << std::endl;
    close(server_socket);
    return 1;
  }

  if (listen(server_socket, 5) < 0) {
    std::cerr << "Listen failed: " << strerror(errno) << std::endl;
    close(server_socket);
    return 1;
  }

  std::cout << "Server is listening..." << std::endl;

  auto ensure_result = ensure_better_auto_mode();
  if (ensure_result != "OK") {
    std::cerr << "Failed to enforce initial BETTER_AUTO mode: " << ensure_result
              << std::endl;
  }

  while (true) {
    client_socket = accept(server_socket, nullptr, nullptr);
    if (client_socket < 0) {
      perror("accept");
      continue;
    }

    on_client_connected();

    while (true) {
      uint32_t cmd_len;
      if (!read_all(client_socket, &cmd_len, sizeof(cmd_len))) {
        std::cerr << "Client disconnected or error occurred while reading "
                     "command length.\n";
        break;
      }

      if (cmd_len > 1024) { // Basic sanity check
        std::cerr << "Command too long. Closing connection.\n";
        break;
      }

      std::vector<char> buffer(cmd_len);
      if (!read_all(client_socket, buffer.data(), cmd_len)) {
        std::cerr
            << "Client disconnected or error occurred while reading command.\n";
        break;
      }

      handle_command(std::string(buffer.begin(), buffer.end()), client_socket);
    }

    close(client_socket);
    on_client_disconnected();
  }

  close(server_socket);
  return 0;
}
