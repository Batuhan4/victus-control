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
#include <cerrno>
#include <cstring>
#include <csignal>

#include "fan.hpp"
#include "keyboard.hpp"

#define SOCKET_DIR "/run/victus-control"
#define SOCKET_PATH SOCKET_DIR "/victus_backend.sock"

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

	if (command == "GET_FAN_SPEED")
	{
        std::string fan_num;
        ss >> fan_num;
        // Validate fan number
        if (fan_num != "1" && fan_num != "2") {
            response = "ERROR: Invalid fan number. Must be 1 or 2";
        } else {
		    response = get_fan_speed(fan_num);
        }
	}
	else if (command == "SET_FAN_SPEED")
	{
		std::string fan_num;
        std::string speed;
        ss >> fan_num >> speed;
        if (fan_num.empty() || speed.empty()) {
            response = "ERROR: Invalid SET_FAN_SPEED command format";
        } else if (fan_num != "1" && fan_num != "2") {
            response = "ERROR: Invalid fan number. Must be 1 or 2";
        } else {
            // Validate speed is numeric
            try {
                int speed_val = std::stoi(speed);
                if (speed_val < 0) {
                    response = "ERROR: Speed must be non-negative";
                } else {
		            response = set_fan_speed(fan_num, speed, true, true);
                }
            } catch (const std::exception &) {
                response = "ERROR: Speed must be a valid number";
            }
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
		    response = set_fan_mode(mode);
            if (response == "OK") {
		        fan_mode_trigger(mode);
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
		std::string r, g, b;
        ss >> r >> g >> b;
        if (r.empty() || g.empty() || b.empty()) {
            response = "ERROR: Invalid SET_KEYBOARD_COLOR command format";
        } else {
            // Validate RGB values
            try {
                int r_val = std::stoi(r);
                int g_val = std::stoi(g);
                int b_val = std::stoi(b);
                if (r_val < 0 || r_val > 255 || g_val < 0 || g_val > 255 || b_val < 0 || b_val > 255) {
                    response = "ERROR: RGB values must be between 0 and 255";
                } else {
		            response = set_keyboard_color(r + " " + g + " " + b);
                }
            } catch (const std::exception &) {
                response = "ERROR: RGB values must be valid numbers";
            }
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
        if (value.empty()) {
            response = "ERROR: Invalid SET_KBD_BRIGHTNESS command format";
        } else {
            // Validate brightness value
            try {
                int brightness = std::stoi(value);
                if (brightness < 0 || brightness > 255) {
                    response = "ERROR: Brightness must be between 0 and 255";
                } else {
		            response = set_keyboard_brightness(value);
                }
            } catch (const std::exception &) {
                response = "ERROR: Brightness must be a valid number";
            }
        }
	}
	else
		response = "ERROR: Unknown command";

    uint32_t len = response.length();
    if (!send_all(client_socket, &len, sizeof(len))) return;
    if (!send_all(client_socket, response.c_str(), len)) return;
}

int main()
{
	int server_socket, client_socket;
	struct sockaddr_un server_addr;

	// Create socket directory if it doesn't exist
	if (mkdir(SOCKET_DIR, 0755) == -1 && errno != EEXIST) {
		std::cerr << "Failed to create socket directory: " << strerror(errno) << std::endl;
		return 1;
	}

	unlink(SOCKET_PATH);

	server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (server_socket < 0)
	{
		std::cerr << "Error creating socket: " << strerror(errno) << std::endl;
		return 1;
	}

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sun_family = AF_UNIX;
	strncpy(server_addr.sun_path, SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

	if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
	{
		std::cerr << "Bind failed: " << strerror(errno) << std::endl;
		close(server_socket);
		return 1;
	}

	if (chmod(SOCKET_PATH, 0660) < 0)
	{
		std::cerr << "Failed to set socket permissions: " << strerror(errno) << std::endl;
		close(server_socket);
		return 1;
	}

	if (listen(server_socket, 5) < 0)
	{
		std::cerr << "Listen failed: " << strerror(errno) << std::endl;
		close(server_socket);
		return 1;
	}

	std::cout << "Server is listening..." << std::endl;

	// Install signal handlers for clean shutdown
	signal(SIGINT, [](int) { 
		std::cout << "\nReceived SIGINT, shutting down..." << std::endl;
		exit(0);
	});
	signal(SIGTERM, [](int) { 
		std::cout << "\nReceived SIGTERM, shutting down..." << std::endl;
		exit(0);
	});

	while (true)
	{
		client_socket = accept(server_socket, nullptr, nullptr);
		if (client_socket < 0)
		{
			perror("accept");
			continue;
		}

		std::cout << "Client connected" << std::endl;

		while (true)
		{
            uint32_t cmd_len;
            if (!read_all(client_socket, &cmd_len, sizeof(cmd_len))) {
                std::cerr << "Client disconnected or error occurred while reading command length.\n";
                break;
            }

            if (cmd_len == 0 || cmd_len > 1024) { // Basic sanity check
                std::cerr << "Invalid command length (" << cmd_len << "). Closing connection.\n";
                break;
            }

            std::vector<char> buffer(cmd_len);
            if (!read_all(client_socket, buffer.data(), cmd_len)) {
                std::cerr << "Client disconnected or error occurred while reading command.\n";
                break;
            }

			handle_command(std::string(buffer.begin(), buffer.end()), client_socket);
		}

		close(client_socket);
		std::cout << "Client disconnected" << std::endl;
	}

	close(server_socket);
	unlink(SOCKET_PATH); // Clean up socket file
	return 0;
}
