#include "net/ipv4.hpp"
#include <stdexcept>

std::uint16_t internetChecksum(const std::vector<unsigned char> &data)
{
    std::uint32_t sum = 0;

    for (std::size_t i = 0; i < data.size(); i += 2)
    {
        std::uint16_t word =
            static_cast<std::uint16_t>(data[i]) << 8;

        if (i + 1 < data.size())
        {
            word |= data[i + 1]; // this is a bitwise or operation (word = word | data[i+1])
        }

        sum += word;

        if (sum > 0xFFFF)
        {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
    }
    
    return static_cast<std::uint16_t>(~sum);
}

Ipv4Packet Ipv4Packet::parse(const std::vector<unsigned char> &data)
{
    if (data.size() < 20)
    {
        throw std::runtime_error("IPV4 packet is too small!");
    }

    Ipv4Packet packet;

    packet.version_ = data[0] >> 4;
    packet.header_length_ = data[0] & 0x0F;

    if (packet.version_ != 4)
    {
        throw std::runtime_error("Not an IPv4 packet");
    }

    if (packet.header_length_ < 5)
    {
        throw std::runtime_error("Invalid IPv4 header length");
    }

    packet.tos_ = data[1];

    packet.total_length_ = (static_cast<std::uint16_t>(data[2]) << 8) | // the OR (|) sums the bits
                           (static_cast<std::uint16_t>(data[3]));

    packet.identification_ =
        (static_cast<std::uint16_t>(data[4]) << 8) |
        static_cast<std::uint16_t>(data[5]);

    std::uint16_t flagsAndOffset = (static_cast<std::uint16_t>(data[6])) |
                                   (static_cast<std::uint16_t>(data[7]));

    packet.flags_ = static_cast<std::uint8_t>(flagsAndOffset >> 13);

    packet.fragment_offset_ = flagsAndOffset & 0x1FFF;

    packet.ttl_ = data[8];
    packet.protocol_ = data[9];

    packet.checksum_ =
        (static_cast<std::uint16_t>(data[10]) << 8) |
        static_cast<std::uint16_t>(data[11]);

    packet.source_ip_ =
        (static_cast<std::uint32_t>(data[12]) << 24) |
        (static_cast<std::uint32_t>(data[13]) << 16) |
        (static_cast<std::uint32_t>(data[14]) << 8) |
        static_cast<std::uint32_t>(data[15]);

    packet.destination_ip_ =
        (static_cast<std::uint32_t>(data[16]) << 24) |
        (static_cast<std::uint32_t>(data[17]) << 16) |
        (static_cast<std::uint32_t>(data[18]) << 8) |
        static_cast<std::uint32_t>(data[19]);

    return packet;
}