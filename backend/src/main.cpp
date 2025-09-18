#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>      // Added for memset, strncpy, strerror
#include <cerrno>       // Added for errno
#include <csignal>      // Added for signal handling
#include <atomic>

#include "fan.hpp"
#include "keyboard.hpp"
#include "config.hpp"
#include "validation.hpp"
#include "logger.hpp"

// Global flag for shutdown
static std::atomic<bool> g_shutdown(false);

// Helper function to reliably send a block of data
bool send_all(int socket, const void *buffer, size_t length) {
    const char *ptr = static_cast<const char*>(buffer);
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
    char *ptr = static_cast<char*>(buffer);
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


static std::string trim(const std::string &input)
{
    size_t start = input.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = input.find_last_not_of(" \t\r\n");
    return input.substr(start, end - start + 1);
}

static std::string normalize_mode(std::string mode)
{
    for (char &ch : mode) {
        if (ch == '-' || ch == ' ') {
            ch = '_';
        } else {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
    }
    return mode;
}

void handle_command(const std::string &command_str, int client_socket)
{
    std::stringstream ss(command_str);
    std::string command;
    ss >> command;

    std::string response;
    
    LOG_DEBUG("Received command: " + command);

    if (command == "GET_FAN_SPEED")
    {
        std::string fan_num;
        ss >> fan_num;
        if (Validation::validate_fan_number(fan_num)) {
            response = get_fan_speed(fan_num);
        } else {
            response = "ERROR: Invalid fan number";
            LOG_WARNING("Invalid fan number: " + fan_num);
        }
    }
    else if (command == "SET_FAN_SPEED")
    {
        std::string fan_num;
        std::string speed;
        ss >> fan_num >> speed;
        
        if (!fan_num.empty() && !speed.empty()) {
            if (!Validation::validate_fan_number(fan_num)) {
                response = "ERROR: Invalid fan number";
                LOG_WARNING("Invalid fan number: " + fan_num);
            } else {
                int parsed_speed;
                if (Validation::validate_fan_speed(speed, parsed_speed)) {
                    response = set_fan_speed(fan_num, std::to_string(parsed_speed), true, true);
                } else {
                    response = "ERROR: Invalid fan speed value";
                    LOG_WARNING("Invalid fan speed: " + speed);
                }
            }
        } else {
            response = "ERROR: Invalid SET_FAN_SPEED command format";
        }
    }
    else if (command == "SET_FAN_MODE")
    {
        std::string remainder;
        std::getline(ss, remainder);
        remainder = trim(remainder);
        if (remainder.empty()) {
            response = "ERROR: Invalid SET_FAN_MODE command format";
        } else {
            std::string mode = normalize_mode(remainder);
            if (Validation::validate_fan_mode(mode)) {
                response = set_fan_mode(mode);
                if (response == "OK") {
                    fan_mode_trigger(mode);
                }
            } else {
                response = "ERROR: Invalid fan mode";
                LOG_WARNING("Invalid fan mode: " + mode);
            }
        }
    }
    else if (command == "GET_FAN_MODE")
    {
        response = get_fan_mode();
    }
    else if (command == "GET_KEYBOARD_COLOR")
    {
        response = get_keyboard_color();
    }
    else if (command == "SET_KEYBOARD_COLOR")
    {
        std::string color_str;
        std::getline(ss, color_str);
        color_str = trim(color_str);
        
        int r, g, b;
        if (Validation::validate_rgb_color(color_str, r, g, b)) {
            response = set_keyboard_color(std::to_string(r) + " " + std::to_string(g) + " " + std::to_string(b));
        } else {
            response = "ERROR: Invalid RGB color values";
            LOG_WARNING("Invalid RGB color: " + color_str);
        }
    }
    else if (command == "GET_KBD_BRIGHTNESS")
    {
        response = get_keyboard_brightness();
    }
    else if (command == "SET_KBD_BRIGHTNESS")
    {
        std::string value;
        ss >> value;
        int brightness;
        if (Validation::validate_brightness(value, brightness)) {
            response = set_keyboard_brightness(std::to_string(brightness));
        } else {
            response = "ERROR: Invalid brightness value";
            LOG_WARNING("Invalid brightness: " + value);
        }
    }
    else {
        response = "ERROR: Unknown command";
        LOG_WARNING("Unknown command received: " + command);
    }

    uint32_t len = response.length();
    if (!send_all(client_socket, &len, sizeof(len))) return;
    if (!send_all(client_socket, response.c_str(), len)) return;
}

// Signal handler
void signal_handler(int sig) {
    LOG_INFO("Received signal " + std::to_string(sig) + ", shutting down");
    g_shutdown.store(true, std::memory_order_release);
}

int main()
{
	int server_socket, client_socket;
	struct sockaddr_un server_addr;
	
	// Set up signal handlers
	std::signal(SIGINT, signal_handler);
	std::signal(SIGTERM, signal_handler);
	
	LOG_INFO("Starting victus-backend service");

	unlink(Config::SOCKET_PATH);

	server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (server_socket < 0)
	{
		LOG_CRITICAL("Error creating socket: " + std::string(strerror(errno)));
		return 1;
	}

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sun_family = AF_UNIX;
	strncpy(server_addr.sun_path, Config::SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

	if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
	{
		LOG_CRITICAL("Bind failed: " + std::string(strerror(errno)));
		close(server_socket);
		return 1;
	}

	if (chmod(Config::SOCKET_PATH, 0660) < 0)
	{
		LOG_ERROR("Failed to set socket permissions: " + std::string(strerror(errno)));
		close(server_socket);
		return 1;
	}

	if (listen(server_socket, 5) < 0)
	{
		LOG_CRITICAL("Listen failed: " + std::string(strerror(errno)));
		close(server_socket);
		return 1;
	}

	LOG_INFO("Server is listening on " + std::string(Config::SOCKET_PATH));

	while (!g_shutdown.load(std::memory_order_acquire))
	{
		// Use select with timeout to allow checking shutdown flag
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(server_socket, &readfds);
		
		struct timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		
		int result = select(server_socket + 1, &readfds, nullptr, nullptr, &timeout);
		if (result < 0) {
			if (errno == EINTR) continue;
			LOG_ERROR("select() failed: " + std::string(strerror(errno)));
			break;
		}
		
		if (result == 0 || !FD_ISSET(server_socket, &readfds)) {
			continue; // Timeout or no activity
		}
		
		client_socket = accept(server_socket, nullptr, nullptr);
		if (client_socket < 0)
		{
			if (errno == EINTR) continue;
			LOG_ERROR("accept() failed: " + std::string(strerror(errno)));
			continue;
		}

		LOG_INFO("Client connected");

		while (!g_shutdown.load(std::memory_order_acquire))
		{
            uint32_t cmd_len;
            if (!read_all(client_socket, &cmd_len, sizeof(cmd_len))) {
                LOG_INFO("Client disconnected or error occurred while reading command length");
                break;
            }

            if (cmd_len > Config::MAX_COMMAND_SIZE) { // Basic sanity check
                LOG_WARNING("Command too long (" + std::to_string(cmd_len) + " bytes). Closing connection");
                break;
            }

            std::vector<char> buffer(cmd_len);
            if (!read_all(client_socket, buffer.data(), cmd_len)) {
                LOG_INFO("Client disconnected or error occurred while reading command");
                break;
            }

			handle_command(std::string(buffer.begin(), buffer.end()), client_socket);
		}

		close(client_socket);
		LOG_INFO("Client disconnected");
	}

	LOG_INFO("Shutting down server");
	
	// Cleanup fan subsystem
	fan_cleanup();
	
	close(server_socket);
	unlink(Config::SOCKET_PATH);
	
	LOG_INFO("Server shutdown complete");
	return 0;
}
