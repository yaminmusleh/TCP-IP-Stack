#include "net/udp.hpp"
#include <stdexcept>

UdpPacket::UdpPacket(std::uint16_t sourcePort, std::uint16_t destinationPort,
                     const std::vector<unsigned char> &payload)
    : sourcePort_(sourcePort), destinationPort_(destinationPort),
      length_(8 + payload.size()), checksum_(0), payload_(payload)
{
}

UdpPacket UdpPacket::parse(const std::vector<unsigned char> &data)
{
    if (data.size() < 8)
    {
        throw std::runtime_error("UDP packet is too short");
    }

    std::uint16_t sourcePort = (static_cast<std::uint16_t>(data[0]) << 8) |
                               (static_cast<std::uint16_t>(data[1]));

    std::uint16_t destinationPort = (static_cast<std::uint16_t>(data[2]) << 8) |
                                    (static_cast<std::uint16_t>(data[3]));

    std::uint16_t length = (static_cast<std::uint16_t>(data[4]) << 8) |
                           (static_cast<std::uint16_t>(data[5]));

    std::uint16_t checksum = (static_cast<std::uint16_t>(data[6]) << 8) |
                             (static_cast<std::uint16_t>(data[7]));

    if (length < 8)
    {
        throw std::runtime_error("Invalid UDP length");
    }

    if (length > data.size())
    {
        throw std::runtime_error("UDP packet is truncated");
    }

    std::vector<unsigned char> payload(
        data.begin() + 8, data.begin() + length);

    UdpPacket packet(sourcePort, destinationPort, payload);

    packet.checksum_ = checksum;

    return packet;
}

std::vector<unsigned char> UdpPacket::buildPseudoHeader(std::uint32_t sourceIp, std::uint32_t destinationIp,
                                       std::uint16_t udpLength)
{
    std::vector<unsigned char> pseudoHeader;
    pseudoHeader.reserve(12);

    // Source IP
    pseudoHeader.push_back(static_cast<unsigned char>(sourceIp >> 24));
    pseudoHeader.push_back(static_cast<unsigned char>(sourceIp >> 16));
    pseudoHeader.push_back(static_cast<unsigned char>(sourceIp >> 8));
    pseudoHeader.push_back(static_cast<unsigned char>(sourceIp));

    // Destination IP
    pseudoHeader.push_back(static_cast<unsigned char>(destinationIp >> 24));
    pseudoHeader.push_back(static_cast<unsigned char>(destinationIp >> 16));
    pseudoHeader.push_back(static_cast<unsigned char>(destinationIp >> 8));
    pseudoHeader.push_back(static_cast<unsigned char>(destinationIp));

    // Zero
    pseudoHeader.push_back(0);

    // Protocol (UDP is 17)
    pseudoHeader.push_back(17);

    // UDP Length
    pseudoHeader.push_back(static_cast<unsigned char>(udpLength >> 8));
    pseudoHeader.push_back(static_cast<unsigned char>(udpLength));

    return pseudoHeader;
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

std::vector<unsigned char> UdpPacket::serialize() const
{
    std::vector<unsigned char> data;
    data.reserve(8 + payload_.size());

    // Source Port
    data.push_back(static_cast<unsigned char>(sourcePort_ >> 8));
    data.push_back(static_cast<unsigned char>(sourcePort_));

    // Destination Port
    data.push_back(static_cast<unsigned char>(destinationPort_ >> 8));
    data.push_back(static_cast<unsigned char>(destinationPort_));

    // Length
    data.push_back(static_cast<unsigned char>(length_ >> 8));
    data.push_back(static_cast<unsigned char>(length_));

    // Checksum
    data.push_back(static_cast<unsigned char>(checksum_ >> 8));
    data.push_back(static_cast<unsigned char>(checksum_));

    // Payload
    data.insert(data.end(), payload_.begin(), payload_.end());

    return data;
}