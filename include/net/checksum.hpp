#pragma once

#include <cstdint>
#include <vector>

std::uint16_t internetChecksum(
    const std::vector<unsigned char> &data);