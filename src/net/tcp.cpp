#include "net/tcp.hpp"
#include "net/checksum.hpp"

#include <stdexcept>
#include <vector>

namespace
{
    constexpr std::uint8_t IP_PROTOCOL_TCP = 6;

    std::vector<unsigned char> buildTcpPseudoHeader(std::uint32_t sourceIp,
                                                    std::uint32_t destinationIp,
                                                    std::uint16_t tcpLength)
    {
        std::vector<unsigned char> p;
        p.reserve(12);

        p.push_back(static_cast<unsigned char>(sourceIp >> 24));
        p.push_back(static_cast<unsigned char>(sourceIp >> 16));
        p.push_back(static_cast<unsigned char>(sourceIp >> 8));
        p.push_back(static_cast<unsigned char>(sourceIp));

        p.push_back(static_cast<unsigned char>(destinationIp >> 24));
        p.push_back(static_cast<unsigned char>(destinationIp >> 16));
        p.push_back(static_cast<unsigned char>(destinationIp >> 8));
        p.push_back(static_cast<unsigned char>(destinationIp));

        p.push_back(0);
        p.push_back(IP_PROTOCOL_TCP);

        p.push_back(static_cast<unsigned char>(tcpLength >> 8));
        p.push_back(static_cast<unsigned char>(tcpLength));

        return p;
    }
}

TcpSegment::TcpSegment(std::uint16_t sourcePort, std::uint16_t destinationPort,
                       std::uint32_t sequenceNumber, std::uint32_t acknowledgmentNumber,
                       std::uint8_t flags, std::uint16_t window,
                       const std::vector<unsigned char> &payload)
    : sourcePort_(sourcePort), destinationPort_(destinationPort),
      sequenceNumber_(sequenceNumber), acknowledgmentNumber_(acknowledgmentNumber),
      flags_(flags), window_(window), payload_(payload)
{
    // 65535 (max IPv4 total length) - 20 (IPv4 header) - 20 (minimum TCP header)
    if (payload.size() > 65495)
    {
        throw std::length_error("TCP payload is too large.");
    }
}

TcpSegment TcpSegment::parse(const std::vector<unsigned char> &data)
{
    if (data.size() < 20)
    {
        throw std::runtime_error("TCP segment is too short");
    }

    const std::size_t headerLength = static_cast<std::size_t>(data[12] >> 4) * 4;

    if (headerLength < 20)
    {
        throw std::runtime_error("Invalid TCP data offset");
    }

    if (headerLength > data.size())
    {
        throw std::runtime_error("TCP header is larger than the segment");
    }

    TcpSegment segment(0, 0, 0, 0, 0, 0);

    segment.sourcePort_ = (static_cast<std::uint16_t>(data[0]) << 8) | data[1];
    segment.destinationPort_ = (static_cast<std::uint16_t>(data[2]) << 8) | data[3];

    segment.sequenceNumber_ = (static_cast<std::uint32_t>(data[4]) << 24) |
                              (static_cast<std::uint32_t>(data[5]) << 16) |
                              (static_cast<std::uint32_t>(data[6]) << 8) |
                              static_cast<std::uint32_t>(data[7]);

    segment.acknowledgmentNumber_ = (static_cast<std::uint32_t>(data[8]) << 24) |
                                    (static_cast<std::uint32_t>(data[9]) << 16) |
                                    (static_cast<std::uint32_t>(data[10]) << 8) |
                                    static_cast<std::uint32_t>(data[11]);

    segment.reserved_ = data[12] & 0x0F;
    segment.flags_ = data[13];

    segment.window_ = (static_cast<std::uint16_t>(data[14]) << 8) | data[15];
    segment.checksum_ = (static_cast<std::uint16_t>(data[16]) << 8) | data[17];
    segment.urgentPointer_ = (static_cast<std::uint16_t>(data[18]) << 8) | data[19];

    segment.options_.assign(data.begin() + 20, data.begin() + headerLength);
    segment.payload_.assign(data.begin() + headerLength, data.end());

    return segment;
}