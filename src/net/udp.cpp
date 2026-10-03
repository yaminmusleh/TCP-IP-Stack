#include "net/udp.hpp"
#include <stdexcept>

UdpPacket UdpPacket::parse(const std::vector<unsigned char> &data)
{
    if (data.size() < 8)
    {
        throw std::runtime_error("UDP packet is too short");
    }
    UdpPacket packet;
    packet.sourcePort_ = (static_cast<std::uint16_t>(data[0]) << 8) |
                         (static_cast<std::uint16_t>(data[1]));

    packet.destinationPort_ = (static_cast<std::uint16_t>(data[2]) << 8) |
                              (static_cast<std::uint16_t>(data[3]));

    packet.length_ = (static_cast<std::uint16_t>(data[4]) << 8) |
                     (static_cast<std::uint16_t>(data[5]));

    packet.checksum_ = (static_cast<std::uint16_t>(data[6]) << 8) |
                       (static_cast<std::uint16_t>(data[7]));

    if (packet.length_ < 8)
    {
        throw std::runtime_error(
            "Invalid UDP length");
    }

    if (packet.length_ > data.size())
    {
        throw std::runtime_error(
            "UDP packet is truncated");
    }

    packet.payload_.assign(data.begin() + 8, data.begin() + packet.length_);

    return packet;
}

std::uint16_t UdpPacket::sourcePort() const
{
    return sourcePort_;
}
std::uint16_t UdpPacket::destinationPort() const
{
    return destinationPort_;
}
std::uint16_t UdpPacket::length() const
{
    return length_;
}
std::uint16_t UdpPacket::checksum() const
{
    return checksum_;
}
const std::vector<unsigned char> &UdpPacket::payload() const
{
    return payload_;
}