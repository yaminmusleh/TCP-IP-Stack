#include "net/arp.hpp"
#include <stdexcept>
ArpPacket ArpPacket::parse(const std::vector<unsigned char> &data)
{

    if (data.size() < 28) // i made it 28 because we added sender and target mac and ip
    {
        throw std::runtime_error("ARP Packet is too small");
    }

    ArpPacket packet;

    packet.hardware_type_ = (static_cast<std::uint16_t>(data[0]) << 8) | static_cast<std::uint16_t>(data[1]);

    packet.protocol_type_ = (static_cast<std::uint16_t>(data[2]) << 8) | static_cast<std::uint16_t>(data[3]);

    packet.hardware_size_ = data[4];
    packet.protocol_size_ = data[5];

    if (packet.hardware_size_ != 6 || packet.protocol_size_ != 4)
    {
        throw std::runtime_error("Unsupported ARP address sizes");
    }
    // why 4 and 6? because the parser assumes 6 bytes are MAC Address and 4 bytes are IP address
    // If an ARP packet uses different address sizes, our current parser should reject it rather than interpret its bytes incorrectly.

    packet.op_code_ = (static_cast<std::uint16_t>(data[6]) << 8) | static_cast<std::uint16_t>(data[7]);

    for (std::size_t i = 0; i < 6; i++)
    {
        packet.sender_mac_[i] = data[8 + i];
    } // after the 8 bytes of an ArpPacket it will start extracting the sender mac after it from the arp request

    packet.sender_ip_ = (static_cast<std::uint32_t>(data[14]) << 24) |
                        (static_cast<std::uint32_t>(data[15]) << 16) |
                        (static_cast<std::uint32_t>(data[16]) << 8) |
                        (static_cast<std::uint32_t>(data[17])); // IP addresses are 32 bit long thats why i do shifting

    for (std::size_t i = 0; i < 6; ++i)
    {
        packet.target_mac_[i] = data[18 + i];
    }

    packet.target_ip_ = (static_cast<std::uint32_t>(data[24]) << 24) |
                        (static_cast<std::uint32_t>(data[25]) << 16) |
                        (static_cast<std::uint32_t>(data[26]) << 8) |
                        (static_cast<std::uint32_t>(data[27]));

    return packet;
}

ArpPacket ArpPacket::createReply(const ArpPacket &request,
                                 const MacAddress &ourMac,
                                 std::uint32_t ourIp)
{
    ArpPacket reply;

    reply.hardware_type_ = request.hardwareType();
    reply.protocol_type_ = request.protocolType();

    reply.hardware_size_ = request.hardwareSize();
    reply.protocol_size_ = request.protocolSize();

    reply.op_code_ = 2;

    reply.sender_mac_ = ourMac;
    reply.sender_ip_ = ourIp;

    reply.target_mac_ = request.senderMac();
    reply.target_ip_ = request.senderIp();

    return reply;
}


// Serializing method
std::vector<unsigned char> ArpPacket::serialize() const
{
    std::vector<unsigned char> data(28);

    // hardware section
    data[0] = static_cast<unsigned char>(hardware_type_ >> 8);
    data[1] = static_cast<unsigned char>(hardware_type_);

    // protocol section
    data[2] = static_cast<unsigned char>(protocol_type_ >> 8);
    data[3] = static_cast<unsigned char>(protocol_type_);

    // address sizes
    data[4] = hardware_size_;
    data[5] = protocol_size_;

    // opcode section
    data[6] = static_cast<unsigned char>(op_code_ >> 8);
    data[7] = static_cast<unsigned char>(op_code_);

    // sender MAC
    for (std::size_t i = 0; i < 6; ++i)
    {
        data[8 + i] = sender_mac_[i];
    }

    // sender IP section
    data[14] = static_cast<unsigned char>(sender_ip_ >> 24);
    data[15] = static_cast<unsigned char>(sender_ip_ >> 16);
    data[16] = static_cast<unsigned char>(sender_ip_ >> 8);
    data[17] = static_cast<unsigned char>(sender_ip_);

    // target MAC
    for (std::size_t i = 0; i < 6; ++i)
    {
        data[18 + i] = target_mac_[i];
    }

    // target IP
    data[24] = static_cast<unsigned char>(target_ip_ >> 24);
    data[25] = static_cast<unsigned char>(target_ip_ >> 16);
    data[26] = static_cast<unsigned char>(target_ip_ >> 8);
    data[27] = static_cast<unsigned char>(target_ip_);

    return data;
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
std::uint16_t ArpPacket::protocolSize() const
{
    return protocol_size_;
}
std::uint16_t ArpPacket::hardwareSize() const
{
    return hardware_size_;
}