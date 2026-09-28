#include "net/icmpv4.hpp"
#include "net/checksum.hpp"
#include <vector>
#include <stdexcept>

Icmpv4Packet Icmpv4Packet::parse(const std::vector<unsigned char> &data)
{

    if (data.size() < 4)
    {
        throw std::runtime_error("ICMP packet is too small");

        Icmpv4Packet packet;

        packet.type_ = data[0];

        packet.code_ = data[1];

        packet.checksum = (static_cast<std::uint16_t>(data[2]) << 8) |
                          (static_cast<std::uint16_t>(data[3]));

        if (internetChecksum(data) != 0)
        {
            throw std::runtime_error("Invalid ICMPv4 checksum");
        }

        packet.payload_.assign(
            data.begin() + 4,
            data.begin());
    }
}