#include "net/udp.hpp"
#include <stdexcept>
#include "net/checksum.hpp"

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

std::uint16_t UdpPacket::calcChecksum(std::uint32_t sourceIp, std::uint32_t destinationIp) const
{
    std::vector<unsigned char> pseudoHeader = buildPseudoHeader(sourceIp, destinationIp, length_);
    std::vector<unsigned char> udpData;
    udpData.reserve(length_);

    // Source Port
    udpData.push_back(static_cast<unsigned char>(sourcePort_ >> 8));
    udpData.push_back(static_cast<unsigned char>(sourcePort_));

    // Destination Port
    udpData.push_back(static_cast<unsigned char>(destinationPort_ >> 8));
    udpData.push_back(static_cast<unsigned char>(destinationPort_));

    // Length
    udpData.push_back(static_cast<unsigned char>(length_ >> 8));
    udpData.push_back(static_cast<unsigned char>(length_));

    // Checksum (set to 0 for calculation)
    udpData.push_back(0);
    udpData.push_back(0);
    // Zeroing out these two bytes prevents garbage or previous calculation values from distorting the current mathematical sum.

    // Payload
    udpData.insert(udpData.end(), payload_.begin(), payload_.end());

    // Combine pseudo-header and UDP data
    pseudoHeader.insert(pseudoHeader.end(), udpData.begin(), udpData.end());
    // It appends the raw payload bytes onto the UDP header, and then concatenates the whole packet onto the pseudoHeader buffer to create one contiguous byte array.

    std::uint16_t checksum = internetChecksum(pseudoHeader);

    if (checksum == 0)
    {
        checksum = 0xFFFF;
    }
    // If the calculated checksum evaluates to 0x0000, it is mapped to 0xFFFF (which is mathematically equivalent in one's complement arithmetic) so the receiving system doesn't assume checksums were turned off.

    return checksum;
}

void UdpPacket::setChecksum(std::uint32_t sourceIp, std::uint32_t destinationIp)
{
    checksum_ = calcChecksum(sourceIp, destinationIp);
}

bool UdpPacket::verifyChecksum(std::uint32_t sourceIp, std::uint32_t destinationIp) const
{
    // IPv4 allows a checksum of 0 to mean that no UDP checksum was provided.
    if (checksum_ == 0)
    {
        return true;
    }

    auto pseudoHeader = buildPseudoHeader(sourceIp, destinationIp, length_);
    auto udpData = serialize(); // Serialize the UDP packet to get the raw bytes, including the checksum field.

    // Combine pseudo-header and UDP data
    pseudoHeader.insert(pseudoHeader.end(), udpData.begin(), udpData.end());

    return internetChecksum(pseudoHeader) == 0; // The checksum is valid if the one's complement sum of the pseudo-header and UDP packet (including the checksum field) equals 0.
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