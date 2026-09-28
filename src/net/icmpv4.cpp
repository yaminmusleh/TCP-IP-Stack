#include "net/icmpv4.hpp"
#include "net/checksum.hpp"
#include <vector>
#include <stdexcept>
#include <algorithm>

Icmpv4Packet Icmpv4Packet::parse(const std::vector<unsigned char> &data)
{

    if (data.size() < 4)
    {
        throw std::runtime_error("ICMP packet is too small");
    }

    Icmpv4Packet packet;

    packet.type_ = data[0];

    packet.code_ = data[1];

    packet.checksum_ = (static_cast<std::uint16_t>(data[2]) << 8) |
                       (static_cast<std::uint16_t>(data[3]));

    if (internetChecksum(data) != 0)
    {
        throw std::runtime_error("Invalid ICMPv4 checksum");
    }

    if (packet.type_ == 8 || packet.type_ == 0)
    {
        if (data.size() < 8)
        {
            throw std::runtime_error(
                "ICMP Echo packet is too small");
        }
        
        packet.identifier_ =
            (static_cast<std::uint16_t>(data[4]) << 8) |
            static_cast<std::uint16_t>(data[5]);

        packet.sequence_ =
            (static_cast<std::uint16_t>(data[6]) << 8) |
            static_cast<std::uint16_t>(data[7]);

        packet.payload_.assign(
            data.begin() + 8,
            data.end());
    }

    else
    {
        packet.payload_.assign(
            data.begin() + 4,
            data.end());
    }

    return packet;
}

std::vector<unsigned char> Icmpv4Packet::serialize() const
{
    std::vector<unsigned char> data(
        4 + payload_.size());

    data[0] = type_;
    data[1] = code_;

    // Checksum must be zero while calculating it.
    data[2] = 0;
    data[3] = 0;

    std::copy(
        payload_.begin(),
        payload_.end(),
        data.begin() + 4);

    std::uint16_t checksum =
        internetChecksum(data);

    data[2] =
        static_cast<unsigned char>(checksum >> 8);

    data[3] =
        static_cast<unsigned char>(checksum & 0xFF);

    return data;
}