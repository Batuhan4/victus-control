#include "socket.hpp"
#include <cstdint>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <vector>
#include <cerrno>
#include <future>
#include <mutex>

bool send_all(int socket, const void *buffer, size_t length) {
  const char *ptr = static_cast<const char *>(buffer);
  while (length > 0) {
    ssize_t bytes_sent = send(socket, ptr, length, 0);
    if (bytes_sent < 1) {
      return false;
    }
    ptr += bytes_sent;
    length -= bytes_sent;
  }
  return true;
}

bool read_all(int socket, void *buffer, size_t length) {
  char *ptr = static_cast<char *>(buffer);
  while (length > 0) {
    ssize_t bytes_read = recv(socket, ptr, length, 0);
    if (bytes_read < 1) {
      return false;
    }
    ptr += bytes_read;
    length -= bytes_read;
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


VictusSocketClient::VictusSocketClient(const std::string &path) : socket_path(path), sockfd(-1)
{
  command_prefix_map = {
      {GET_FAN_SPEED, "GET_FAN_SPEED"},
      {SET_FAN_SPEED, "SET_FAN_SPEED"},
      {SET_FAN_MODE, "SET_FAN_MODE"},
      {GET_FAN_MODE, "GET_FAN_MODE"},
      {GET_KEYBOARD_COLOR, "GET_KEYBOARD_COLOR"},
      {SET_KEYBOARD_COLOR, "SET_KEYBOARD_COLOR"},
      {SET_KEYBOARD_ZONE_COLOR, "SET_KEYBOARD_ZONE_COLOR"},
      {GET_KEYBOARD_ZONE_COLOR, "GET_KEYBOARD_ZONE_COLOR"},
      {GET_KBD_BRIGHTNESS, "GET_KBD_BRIGHTNESS"},
      {SET_KBD_BRIGHTNESS, "SET_KBD_BRIGHTNESS"},
      {GET_KEYBOARD_TYPE, "GET_KEYBOARD_TYPE"},
  };

  // Don't connect here, connect on first command
}

VictusSocketClient::~VictusSocketClient()
{
	close_socket();
}

bool VictusSocketClient::connect_to_server()
{
  if (sockfd != -1) {
    return true;
  }

  std::cout << "Connecting to server..." << std::endl;

  sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sockfd == -1)
	{
    std::cerr << "Cannot create socket: " << strerror(errno) << std::endl;
    return false;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

  // Try to connect with a timeout
	if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
	{
		std::cerr << "Failed to connect to the server: " << strerror(errno) << std::endl;
    close(sockfd);
    sockfd = -1;
    return false;
  }

  std::cout << "Connection to server successful." << std::endl;
  return true;
}

void VictusSocketClient::close_socket()
{
  if (sockfd != -1) {
    std::cout << "Closing the connection..." << std::endl;
    close(sockfd);
    sockfd = -1;
    std::cout << "Connection closed." << std::endl;
  }
}

std::string VictusSocketClient::send_command(const std::string &command)
{
  std::lock_guard<std::mutex> lock(socket_mutex);

  if (sockfd == -1) {
    if (!connect_to_server()) {
      return "ERROR: No server connection";
    }
  }

  uint32_t len = static_cast<uint32_t>(command.length());
  if (!send_u32_le(sockfd, len) || !send_all(sockfd, command.c_str(), len)) {
    std::cerr << "Failed to send command, closing socket." << std::endl;
    close_socket();
    return "ERROR: Failed to send command";
  }

  uint32_t response_len = 0;
  if (!read_u32_le(sockfd, &response_len)) {
    std::cerr << "Failed to read response length, closing socket." << std::endl;
    close_socket();
    return "ERROR: Failed to read response length";
  }

  if (response_len > 4096) { // Sanity check
        std::cerr << "Response too long (" << response_len << " bytes), closing socket." << std::endl;
    close_socket();
    return "ERROR: Response too long";
  }

  std::vector<char> buffer(response_len);
  if (!read_all(sockfd, buffer.data(), response_len)) {
    std::cerr << "Failed to read response, closing socket." << std::endl;
    close_socket();
    return "ERROR: Failed to read response";
  }

  return std::string(buffer.begin(), buffer.end());
}

std::future<std::string> VictusSocketClient::send_command_async(ServerCommands type, const std::string &command)
{
	return std::async(std::launch::async, [this, type, command]()
					  {
    auto it = command_prefix_map.find(type);
		if (it != command_prefix_map.end())
		{
      std::string full_command = it->second;
			if (!command.empty())
			{
				if (it->second.back() != ' ')
				{
          full_command += " ";
        }
        full_command += command;
      }
      std::cout << "Sending command: " << full_command << std::endl;
      auto result = send_command(full_command);
      std::cout << "Received response: " << result << std::endl;
      return result;
    }
		return std::string("ERROR: Unknown command type"); });
}
