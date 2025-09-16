#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <signal.h>
#include <atomic>
#include <cerrno>

#include "fan.hpp"
#include "keyboard.hpp"
#include "util.hpp"
#include "constants.hpp"

#define SOCKET_DIR "/run/victus-control"
#define SOCKET_PATH SOCKET_DIR "/victus_backend.sock"

// Global flag for graceful shutdown
static std::atomic<bool> server_running(true);

void signal_handler(int sig) {
	log(LogLevel::Info, "Received signal, shutting down gracefully");
	server_running = false;
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
        if (fan_num.empty()) {
            response = "ERROR: Missing fan number";
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
        } else {
		    response = set_fan_speed(fan_num, speed);
        }
	}
	else if (command == "SET_FAN_MODE")
	{
		std::string mode;
        ss >> mode;
        if (mode.empty()) {
            response = "ERROR: Missing fan mode";
        } else {
		    response = set_fan_mode(mode);
		    if (!is_error(response)) {
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
		    response = set_keyboard_color(r + " " + g + " " + b);
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
            response = "ERROR: Missing brightness value";
        } else {
		    response = set_keyboard_brightness(value);
        }
	}
	else {
		log(LogLevel::Warn, "Unknown command received: " + command);
		response = "ERROR: Unknown command";
	}

    uint32_t len = response.length();
    if (!send_all(client_socket, &len, sizeof(len))) {
        log(LogLevel::Error, "Failed to send response length");
        return;
    }
    if (!send_all(client_socket, response.c_str(), len)) {
        log(LogLevel::Error, "Failed to send response data");
        return;
    }
}

// RAII wrapper for client socket handling
class ClientSocketGuard {
public:
    ClientSocketGuard(int socket) : socket_(socket) {}
    ~ClientSocketGuard() { 
        if (socket_ >= 0) {
            close(socket_);
            log(LogLevel::Info, "Client socket closed");
        }
    }
    int get() const { return socket_; }
private:
    int socket_;
};

void handle_client(int client_socket) {
    ClientSocketGuard guard(client_socket);
    
    log(LogLevel::Info, "Client connected");
    
    while (server_running) {
        uint32_t cmd_len;
        if (!read_all(client_socket, &cmd_len, sizeof(cmd_len))) {
            log(LogLevel::Info, "Client disconnected or error reading command length");
            break;
        }

        // Protocol hardening - reject oversized requests early
        if (cmd_len > MAX_COMMAND_LENGTH) {
            log(LogLevel::Warn, "Command too long (" + std::to_string(cmd_len) + " bytes), closing connection");
            std::string error_response = "ERROR: Request too long";
            uint32_t error_len = error_response.length();
            send_all(client_socket, &error_len, sizeof(error_len));
            send_all(client_socket, error_response.c_str(), error_len);
            break;
        }

        if (cmd_len == 0) {
            log(LogLevel::Warn, "Zero-length command received");
            break;
        }

        std::vector<char> buffer(cmd_len);
        if (!read_all(client_socket, buffer.data(), cmd_len)) {
            log(LogLevel::Info, "Client disconnected or error reading command");
            break;
        }

        handle_command(std::string(buffer.begin(), buffer.end()), client_socket);
    }
}

int main()
{
	// Set up signal handlers for graceful shutdown
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	int server_socket;
	struct sockaddr_un server_addr;

	unlink(SOCKET_PATH);

	server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (server_socket < 0)
	{
		log(LogLevel::Error, "Error creating socket: " + std::string(strerror(errno)));
		return 1;
	}

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sun_family = AF_UNIX;
	strncpy(server_addr.sun_path, SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

	if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
	{
		log(LogLevel::Error, "Bind failed: " + std::string(strerror(errno)));
		close(server_socket);
		return 1;
	}

	if (chmod(SOCKET_PATH, 0660) < 0)
	{
		log(LogLevel::Error, "Failed to set socket permissions: " + std::string(strerror(errno)));
		close(server_socket);
		return 1;
	}

	if (listen(server_socket, 5) < 0)
	{
		log(LogLevel::Error, "Listen failed: " + std::string(strerror(errno)));
		close(server_socket);
		return 1;
	}

	log(LogLevel::Info, "Server is listening...");

	while (server_running)
	{
		int client_socket = accept(server_socket, nullptr, nullptr);
		if (client_socket < 0)
		{
			if (errno == EINTR && !server_running) {
				log(LogLevel::Info, "Accept interrupted, server shutting down");
				break;
			}
			log(LogLevel::Error, "Accept failed: " + std::string(strerror(errno)));
			continue;
		}

		handle_client(client_socket);
		// client_socket is automatically closed by ClientSocketGuard
	}

	log(LogLevel::Info, "Shutting down server");
	cleanup_fan_threads();
	close(server_socket);
	unlink(SOCKET_PATH);
	return 0;
}