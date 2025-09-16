#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <signal.h>

#include "fan.hpp"
#include "keyboard.hpp"

#define SOCKET_DIR "/run/victus-control"
#define SOCKET_PATH SOCKET_DIR "/victus_backend.sock"

static volatile sig_atomic_t server_running = 1;

void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        server_running = 0;
    }
}

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


void handle_command(const std::string &command, int client_socket)
{
	std::string response;

	if (command.find("GET_FAN_SPEED") == 0)
	{
		response = get_fan_speed(command.substr(14));
	}
	else if (command.find("SET_FAN_SPEED") == 0)
	{
		std::string fan_num = command.substr(14, 1);
		std::string speed = command.substr(16);
		response = set_fan_speed(fan_num, speed);
	}
	else if (command.find("SET_FAN_MODE") == 0)
	{
		std::string mode = command.substr(13); // 1 more char for the space
		response = set_fan_mode(mode);
		fan_mode_trigger(mode);
	}
	else if (command == "GET_FAN_MODE")
	{
		response = get_fan_mode();
	}
	else if (command == "GET_KEYBOARD_COLOR")
	{
		response = get_keyboard_color();
	}
	else if (command.find("SET_KEYBOARD_COLOR") == 0)
	{
		std::string color = command.substr(19);
		response = set_keyboard_color(color);
	}
	else if (command == "GET_KBD_BRIGHTNESS")
	{
		response = get_keyboard_brightness();
	}
	else if (command.find("SET_KBD_BRIGHTNESS") == 0)
	{
		std::string value = command.substr(19);
		response = set_keyboard_brightness(value);
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

	// Set up signal handlers for graceful shutdown
	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

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

	while (server_running)
	{
		client_socket = accept(server_socket, nullptr, nullptr);
		if (client_socket < 0)
		{
			if (errno == EINTR) {
				// Interrupted by signal, check if we should continue
				continue;
			}
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

            if (cmd_len > 1024) { // Basic sanity check
                std::cerr << "Command too long. Closing connection.\n";
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
	unlink(SOCKET_PATH);
	std::cout << "Server shutdown gracefully" << std::endl;
	return 0;
}