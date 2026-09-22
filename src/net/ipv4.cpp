#include "net/ipv4.hpp"
#include <stdexcept>

Ipv4Packet Ipv4Packet::parse(const std::vector<unsigned char> &data)
{
    if (data.size() < 20)
    {
        throw std::runtime_error("IPV4 packet is too small!");
    }

    Ipv4Packet packet;

    packet.version_ = data[0] >> 4;
    packet.header_length_ = data[1] & 0x0F;

    if (packet.version_ != 4)
    {
        throw std::runtime_error("Not an IPv4 packet");
    }

    if (packet.header_length_ < 5)
    {
        throw std::runtime_error("Invalid IPv4 header length");
    }

    return packet;
}