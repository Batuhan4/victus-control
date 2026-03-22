#include <array>
#include <iostream>

#include "validation.hpp"

namespace {

bool expect(bool condition, const char *message) {
  if (condition)
    return true;

  std::cerr << "FAILED: " << message << std::endl;
  return false;
}

} // namespace

int main() {
  bool ok = true;

  ok &= expect(normalize_mode("better auto") == "BETTER_AUTO",
               "normalize_mode should uppercase and replace spaces");
  ok &= expect(normalize_mode("max") == "MAX",
               "normalize_mode should uppercase simple values");

  auto fan1 = fan_index_from_string("1");
  auto fan2 = fan_index_from_string("2");
  ok &= expect(fan1 && *fan1 == 0, "fan 1 should map to index 0");
  ok &= expect(fan2 && *fan2 == 1, "fan 2 should map to index 1");
  ok &= expect(!fan_index_from_string("0"),
               "fan number 0 should be rejected");
  ok &= expect(!fan_index_from_string("3"),
               "fan number 3 should be rejected");

  int parsed_value = 0;
  ok &= expect(parse_strict_int("255", &parsed_value) && parsed_value == 255,
               "strict integer parsing should accept plain numbers");
  ok &= expect(parse_strict_int("-1", &parsed_value) && parsed_value == -1,
               "strict integer parsing should accept signed numbers");
  ok &= expect(!parse_strict_int("255rpm", &parsed_value),
               "strict integer parsing should reject trailing characters");
  ok &= expect(!parse_strict_int("", &parsed_value),
               "strict integer parsing should reject empty strings");

  ok &= expect(parse_bounded_int("0", 0, 255, &parsed_value) &&
                   parsed_value == 0,
               "bounded integer parsing should accept the minimum");
  ok &= expect(parse_bounded_int("255", 0, 255, &parsed_value) &&
                   parsed_value == 255,
               "bounded integer parsing should accept the maximum");
  ok &= expect(!parse_bounded_int("256", 0, 255, &parsed_value),
               "bounded integer parsing should reject values above max");
  ok &= expect(!parse_bounded_int("-1", 0, 255, &parsed_value),
               "bounded integer parsing should reject values below min");

  std::array<int, 3> rgb = {};
  ok &= expect(parse_rgb_triplet("12 34 56", &rgb) &&
                   rgb == std::array<int, 3>{12, 34, 56},
               "rgb parsing should accept valid triplets");
  ok &= expect(!parse_rgb_triplet("12 34", &rgb),
               "rgb parsing should reject incomplete triplets");
  ok &= expect(!parse_rgb_triplet("12 34 256", &rgb),
               "rgb parsing should reject out-of-range values");
  ok &= expect(!parse_rgb_triplet("12 34 56 78", &rgb),
               "rgb parsing should reject extra tokens");

  return ok ? 0 : 1;
}
