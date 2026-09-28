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
    bool isEcho =
        (type_ == 8 || type_ == 0);

    std::size_t headerSize = isEcho ? 8 : 4;

    std::vector<unsigned char> data(
        headerSize + payload_.size());

    data[0] = type_;
    data[1] = code_;

    // Checksum must be zero while calculating it.
    data[2] = 0;
    data[3] = 0;

    if (isEcho)
    {
        data[4] =
            static_cast<unsigned char>(identifier_ >> 8);

        data[5] =
            static_cast<unsigned char>(identifier_ & 0xFF);

        data[6] =
            static_cast<unsigned char>(sequence_ >> 8);

        data[7] =
            static_cast<unsigned char>(sequence_ & 0xFF);
    }

    std::copy(
        payload_.begin(),
        payload_.end(),
        data.begin() + headerSize);

    std::uint16_t checksum =
        internetChecksum(data);

    data[2] =
        static_cast<unsigned char>(checksum >> 8);

    data[3] =
        static_cast<unsigned char>(checksum & 0xFF);

    return data;
}

std::uint8_t Icmpv4Packet::type() const
{
    return type_;
}

std::uint8_t Icmpv4Packet::code() const
{
    return code_;
}

std::uint16_t Icmpv4Packet::checksum() const
{
    return checksum_;
}

std::uint16_t Icmpv4Packet::identifier() const
{
    return identifier_;
}

std::uint16_t Icmpv4Packet::sequence() const
{
    return sequence_;
}

const std::vector<unsigned char> &Icmpv4Packet::payload() const
{
    return payload_;
}