#include "net/arp.hpp"
#include <stdexcept>

ArpPacket ArpPacket::parse(const std::vector<unsigned char> &data)
{

    if (data.size() < 28) //i made it 28 because we added sender and target mac and ip 
    {
        throw std::runtime_error("ARP Packet is too small");
    }

    ArpPacket packet;

    packet.hardware_type_ = (static_cast<std::uint16_t>(data[0]) << 8) | static_cast<std::uint16_t>(data[1]);

    packet.protocol_type_ = (static_cast<std::uint16_t>(data[2]) << 8) | static_cast<std::uint16_t>(data[3]);

    packet.op_code_ = (static_cast<std::uint16_t>(data[6]) << 8) | static_cast<std::uint16_t>(data[7]);

    for (std::size_t i = 0; i < 6; i++)
    {
        packet.sender_mac_[i] = data[8 + i];
    } // after the 8 bytes of an ArpPacket it will start extracting the sender mac after it from the arp request

    packet.sender_ip_ = (static_cast<std::uint32_t>(data[14]) << 24) |
                        (static_cast<std::uint32_t>(data[15]) << 16) |
                        (static_cast<std::uint32_t>(data[16]) << 8) |
                        (static_cast<std::uint32_t>(data[17])); // IP addresses are 32 bit long thats why i do shifting

    for (std::size_t i = 0; i < 6; ++i){
        packet.target_mac_[i] = data[18 + i];
    }

    packet.target_ip_ = (static_cast<std::uint32_t>(data[24]) << 24) |
                        (static_cast<std::uint32_t>(data[25]) << 16) |
                        (static_cast<std::uint32_t>(data[26]) << 8) |
                        (static_cast<std::uint32_t>(data[27]));

    return packet;
}
MacAddress ArpPacket::senderMac() const 
{
    return sender_mac_;
}
MacAddress ArpPacket::targetMac() const
{
    return target_mac_;
}
std::uint32_t ArpPacket::senderIp() const
{
    return sender_ip_;
}
std::uint32_t ArpPacket::targetIp() const
{
    return target_ip_;
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