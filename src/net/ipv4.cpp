#include "net/ipv4.hpp"
#include <stdexcept>
#include <algorithm>

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

    if (packet.header_length_ != 5)
    {
        throw std::runtime_error(
            "IPv4 options are not supported");
    }

    std::size_t headerSize =
        static_cast<std::size_t>(packet.header_length_) * 4;

    if (data.size() < headerSize)
    {
        throw std::runtime_error(
            "IPv4 packet is smaller than its header");
    }

    packet.tos_ = data[1];

    packet.total_length_ = (static_cast<std::uint16_t>(data[2]) << 8) | // the OR (|) does a bitwise or operation
                           (static_cast<std::uint16_t>(data[3]));

    packet.identification_ =
        (static_cast<std::uint16_t>(data[4]) << 8) |
        static_cast<std::uint16_t>(data[5]);

    std::uint16_t flagsAndOffset = (static_cast<std::uint16_t>(data[6]) << 8) |
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

    std::vector<unsigned char> header(
        data.begin(),
        data.begin() + headerSize);

    if (internetChecksum(header) != 0)
    {
        throw std::runtime_error(
            "Invalid IPv4 header checksum");
    }

    if (packet.total_length_ < headerSize)
    {
        throw std::runtime_error("IPV4 total length is smaller than header");
    }
    if (data.size() < packet.total_length_)
    {
        throw std::runtime_error("IPV4 packet is truncated");
    }

    packet.payload_.assign(data.begin() + headerSize, data.begin() + packet.total_length_); // extraction (begin from headerSize and end at total length)
    return packet;
}

std::vector<unsigned char> Ipv4Packet::serialize() const
{
    std::vector<unsigned char> data(20 + payload_.size());

    data[0] = static_cast<unsigned char>((version_ << 4) | header_length_);

    data[1] = tos_;

    data[2] = static_cast<unsigned char>(total_length_ >> 8);

    data[3] = static_cast<unsigned char>(total_length_ & 0xFF);

    data[4] =
        static_cast<unsigned char>(identification_ >> 8);

    data[5] =
        static_cast<unsigned char>(identification_ & 0xFF);

    std::uint16_t flagsAndOffset =
        static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(flags_) << 13) |
            fragment_offset_);

    data[6] =
        static_cast<unsigned char>(flagsAndOffset >> 8);

    data[7] =
        static_cast<unsigned char>(flagsAndOffset & 0xFF);

    data[8] = ttl_;
    data[9] = protocol_;

    data[10] = 0;
    data[11] = 0;

    data[12] =
        static_cast<unsigned char>(source_ip_ >> 24);

    data[13] =
        static_cast<unsigned char>((source_ip_ >> 16) & 0xFF);

    data[14] =
        static_cast<unsigned char>((source_ip_ >> 8) & 0xFF);

    data[15] =
        static_cast<unsigned char>(source_ip_ & 0xFF);

    data[16] =
        static_cast<unsigned char>(destination_ip_ >> 24);

    data[17] =
        static_cast<unsigned char>((destination_ip_ >> 16) & 0xFF);

    data[18] =
        static_cast<unsigned char>((destination_ip_ >> 8) & 0xFF);

    data[19] =
        static_cast<unsigned char>(destination_ip_ & 0xFF);

    std::vector<unsigned char> header(data.begin(), data.begin() + 20);

    std::uint16_t checksum = internetChecksum(header);

    data[10] = static_cast<unsigned char>(checksum >> 8);

    data[11] = static_cast<unsigned char>(checksum & 0xFF);

    std::copy(
        payload_.begin(),
        payload_.end(),
        data.begin() + 20);

    return data;
}