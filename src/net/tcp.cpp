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

std::vector<unsigned char> TcpSegment::serializeWithChecksum(std::uint16_t checksum) const
{
    const std::size_t paddedOptions = (options_.size() + 3) & ~static_cast<std::size_t>(3);
    const std::size_t headerLength = 20 + paddedOptions;

    std::vector<unsigned char> data(headerLength + payload_.size(), 0); // dynamic vector of bytes named data
    // headerLength + payload_.size(): This calculates the total initial size of the vector.
    // It adds a fixed header size (headerLength) to the size of an existing payload (payload_.size()) to ensure the vector is big enough to hold both.
    // the 0 tells the c++ to fill every single byte in this newly created vector with zero

    // Ports:
    data[0] = static_cast<unsigned char>(sourcePort_ >> 8);
    data[1] = static_cast<unsigned char>(sourcePort_);

    data[2] = static_cast<unsigned char>(destinationPort_ >> 8);
    data[3] = static_cast<unsigned char>(destinationPort_);

    // Sequence number:
    data[4] = static_cast<unsigned char>(sequenceNumber_ >> 24);
    data[5] = static_cast<unsigned char>(sequenceNumber_ >> 16);
    data[6] = static_cast<unsigned char>(sequenceNumber_ >> 8);
    data[7] = static_cast<unsigned char>(sequenceNumber_);

    // Acknowledgement number:
    data[8] = static_cast<unsigned char>(acknowledgmentNumber_ >> 24);
    data[9] = static_cast<unsigned char>(acknowledgmentNumber_ >> 16);
    data[10] = static_cast<unsigned char>(acknowledgmentNumber_ >> 8);
    data[11] = static_cast<unsigned char>(acknowledgmentNumber_);

    // an important piece:
    data[12] = static_cast<unsigned char>(((headerLength / 4) << 4) | (reserved_ & 0x0F));
    // notes on it:
    // headerLength / 4: Many network protocols (like IPv4) measure header lengths in 32-bit "words" (groups of 4 bytes) rather than individual bytes.
    // Dividing by 4 converts the byte length into a word count. For example, a 20-byte header becomes 5.
    // << 4: This shifts the bits 4 positions to the left. In binary, a byte has 8 bits (0000 0000).
    // If our word count was 5 (0000 0101), shifting it left by 4 turns it into 0101 0000. This frees up the bottom 4 bits.
    // reserved_ & 0x0F: This uses a bitwise AND with 0x0F (which is 0000 1111 in binary) to act as a mask.
    // It ensures that only the lowest 4 bits of the reserved_ variable are kept, erasing anything higher to prevent it from overlapping with the header length.

    // Flags:
    data[13] = flags_;

    // Window:
    data[14] = static_cast<unsigned char>(window_ >> 8);
    data[15] = static_cast<unsigned char>(window_);

    // Checksum:
    data[16] = static_cast<unsigned char>(checksum >> 8);
    data[17] = static_cast<unsigned char>(checksum);

    // Urge pointer:
    data[18] = static_cast<unsigned char>(urgentPointer_ >> 8);
    data[19] = static_cast<unsigned char>(urgentPointer_);

    for (std::size_t i = 0; i < options_.size(); ++i)
    {
        data[20 + i] = options_[i];
    }

    for (std::size_t i = 0; i < payload_.size(); ++i)
    {
        data[headerLength + i] = payload_[i];
    }

    return data;
}

std::vector<unsigned char> TcpSegment::serialize() const
{
    return serializeWithChecksum(checksum_);
}

std::uint16_t TcpSegment::calcChecksum(std::uint32_t sourceIp, std::uint32_t destinationIp) const
{
    auto segment = serializeWithChecksum(0);

    if (segment.size() > 65535)
    {
        throw std::length_error("TCP segment is too large for a checksum");
    }

    auto buffer = buildTcpPseudoHeader(sourceIp, destinationIp, static_cast<std::uint16_t>(segment.size()));

    buffer.insert(buffer.end(), segment.begin(), segment.end());

    // Unlike UDP, a computed TCP checksum of 0x0000 is valid and is sent as-is.
    return internetChecksum(buffer);
}

void TcpSegment::setChecksum(std::uint32_t sourceIp, std::uint32_t destinationIp)
{
    checksum_ = calcChecksum(sourceIp, destinationIp);
}

bool TcpSegment::verifyChecksum(std::uint32_t sourceIp, std::uint32_t destinationIp) const
{
    auto segment = serialize();

    if (segment.size() > 65535)
    {
        return false;
    }

    auto buffer = buildTcpPseudoHeader(sourceIp, destinationIp,
                                       static_cast<std::uint16_t>(segment.size()));
    buffer.insert(buffer.end(), segment.begin(), segment.end());

    return internetChecksum(buffer) == 0;
}

void TcpSegment::setMss(std::uint16_t mss)
{
    if (options_.size() + 4 > 40)
    {
        throw std::length_error("TCP options are full");
    }

    options_.push_back(2); // kind: Maximum Segment Size
    options_.push_back(4); // length
    options_.push_back(static_cast<unsigned char>(mss >> 8));
    options_.push_back(static_cast<unsigned char>(mss));
}

std::optional<std::uint16_t> TcpSegment::mss() const
{
    std::size_t i = 0;

    while (i < options_.size())
    {
        const unsigned char kind = options_[i];

        if (kind == 0)
        { // end of options list
            break;
        }

        if (kind == 1)
        { // no opertaions
            ++i;
            continue;
        }

        if (i + 1 >= options_.size())
        {
            break;
        }

        const std::size_t length = options_[i + 1];

        if (length < 2 || i + length > options_.size())
        {
            break; // malformed option; stop rather than read out of bounds
        }
        if (kind == 2 && length == 4)
        {
            return static_cast<std::uint16_t>((options_[i + 2] << 8) | options_[i + 3]);
        }

        i += length;
    }

    return std::nullopt;
}

std::uint32_t TcpSegment::sequenceLength() const
{
    std::uint32_t length = static_cast<std::uint32_t>(payload_.size());

    if (hasFlag(SYN))
    {
        ++length;
    }

    if (hasFlag(FIN))
    {
        ++length;
    }

    return length;
}

std::uint16_t TcpSegment::sourcePort() const { return sourcePort_; }
std::uint16_t TcpSegment::destinationPort() const { return destinationPort_; }
std::uint32_t TcpSegment::sequenceNumber() const { return sequenceNumber_; }
std::uint32_t TcpSegment::acknowledgmentNumber() const { return acknowledgmentNumber_; }
std::uint8_t TcpSegment::flags() const { return flags_; }
bool TcpSegment::hasFlag(std::uint8_t flag) const { return (flags_ & flag) != 0; }
std::uint16_t TcpSegment::window() const { return window_; }
std::uint16_t TcpSegment::checksum() const { return checksum_; }
std::uint16_t TcpSegment::urgentPointer() const { return urgentPointer_; }
const std::vector<unsigned char> &TcpSegment::options() const { return options_; }
const std::vector<unsigned char> &TcpSegment::payload() const { return payload_; }

std::size_t TcpSegment::headerLength() const
{
    return 20 + ((options_.size() + 3) & ~static_cast<std::size_t>(3));
}