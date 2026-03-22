#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>

std::string normalize_mode(std::string mode);
std::optional<size_t> fan_index_from_string(const std::string &fan_num);

bool parse_strict_int(const std::string &value, int *parsed);
bool parse_bounded_int(const std::string &value, int min_value, int max_value,
                       int *parsed);

bool parse_rgb_triplet(const std::string &color, std::array<int, 3> *rgb);
