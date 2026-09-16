#include "net/arp.hpp"
#include <stdexcept>

ArpPacket ArpPacket::parse(const std::vector<unsigned char> &data)
{

    if (data.size() < 8)
    {
        throw std::runtime_error("ARP Packet is too small");
    }

    ArpPacket packet;

    packet.hardware_type_ = (static_cast<std::uint16_t>(data[0]) << 8) | static_cast<std::uint16_t>(data[1]);

    packet.protocol_type_ = (static_cast<std::uint16_t>(data[2]) << 8) | static_cast<std::uint16_t>(data[3]);

    packet.op_code_ = (static_cast<std::uint16_t>(data[6]) << 8) | static_cast<std::uint16_t>(data[7]);

    return packet;
}

std::uint16_t ArpPacket::hardwareType() const
{
    return hardware_type_;
}
std::uint16_t ArpPacket::protocolType() const
{
    return protocol_type_;
}
std::uint16_t ArpPacket::opCode() const
{
    return op_code_;
}