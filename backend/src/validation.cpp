#include "validation.hpp"

#include <cctype>
#include <sstream>

std::string normalize_mode(std::string mode) {
  for (char &ch : mode) {
    if (ch == '-' || ch == ' ') {
      ch = '_';
    } else {
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
  }
  return mode;
}

std::optional<size_t> fan_index_from_string(const std::string &fan_num) {
  if (fan_num == "1")
    return 0;

  if (fan_num == "2")
    return 1;

  return std::nullopt;
}

bool parse_strict_int(const std::string &value, int *parsed) {
  if (!parsed || value.empty())
    return false;

  size_t position = 0;

  try {
    int parsed_value = std::stoi(value, &position);
    if (position != value.size())
      return false;

    *parsed = parsed_value;
    return true;
  } catch (...) {
    return false;
  }
}

bool parse_bounded_int(const std::string &value, int min_value, int max_value,
                       int *parsed) {
  int parsed_value = 0;
  if (!parse_strict_int(value, &parsed_value))
    return false;

  if (parsed_value < min_value || parsed_value > max_value)
    return false;

  if (parsed)
    *parsed = parsed_value;

  return true;
}

bool parse_rgb_triplet(const std::string &color, std::array<int, 3> *rgb) {
  if (!rgb)
    return false;

  std::stringstream ss(color);
  int red = 0;
  int green = 0;
  int blue = 0;
  char extra = '\0';

  if (!(ss >> red >> green >> blue))
    return false;

  if (ss >> extra)
    return false;

  if (red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 ||
      blue > 255)
    return false;

  *rgb = {red, green, blue};
  return true;
}
