#ifndef VICTUS_SOCKET_HPP
#define VICTUS_SOCKET_HPP

#include <string>
#include <future>
#include <unordered_map>
#include <functional>
#include <mutex>

enum ServerCommands
{
  GET_FAN_SPEED,
  SET_FAN_SPEED,
  SET_FAN_MODE,
  GET_FAN_MODE,
  GET_FAN_TARGET_SUPPORT,
  GET_KEYBOARD_COLOR,
  SET_KEYBOARD_COLOR,
  SET_KEYBOARD_ZONE_COLOR,
  GET_KEYBOARD_ZONE_COLOR,
  GET_KBD_BRIGHTNESS,
  SET_KBD_BRIGHTNESS,
  GET_KBD_EFFECT,
  SET_KBD_EFFECT,
  GET_KEYBOARD_TYPE,
  GET_CPU_TEMP,
  GET_GPU_TEMP
};

class VictusSocketClient
{
public:
  VictusSocketClient(const std::string &socket_path);
  ~VictusSocketClient();

  std::future<std::string> send_command_async(ServerCommands type, const std::string &command = "");

private:
  std::string send_command(const std::string &command);
  std::string socket_path;

  bool connect_to_server();
  void close_socket();

  int sockfd;
  std::mutex socket_mutex;

  std::unordered_map<ServerCommands, std::string> command_prefix_map;
};

#endif // VICTUS_SOCKET_HPP
