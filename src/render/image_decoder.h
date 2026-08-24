#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vitamaps {

bool decode_png_rgba(const std::vector<std::uint8_t> &encoded,
                     int expected_size, int &width, int &height,
                     std::vector<std::uint8_t> &rgba,
                     std::string &error);

} // namespace vitamaps
