#include "net/checksum.hpp"

std::uint16_t internetChecksum(
    const std::vector<unsigned char> &data)
{
    std::uint32_t sum = 0;

    for (std::size_t i = 0; i < data.size(); i += 2)
    {
        std::uint16_t word =
            static_cast<std::uint16_t>(data[i]) << 8;

        if (i + 1 < data.size())
        {
            word |= data[i + 1];
        }

        sum += word;

        if (sum > 0xFFFF)
        {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
    }

    return static_cast<std::uint16_t>(~sum);
}