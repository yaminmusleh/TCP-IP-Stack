#pragma once
#include <cstdint>
#include <vector>

class UdpPacket
{

public:
    static UdpPacket parse(const std::vector<unsigned char> &data);

    std::vector<unsigned char> serialize() const;

    std::uint16_t sourcePort() const;
    std::uint16_t destinationPort() const;
    std::uint16_t length() const;
    std::uint16_t checksum() const;
    const std::vector<unsigned char> &payload() const;

private:
    std::uint16_t sourcePort_ = 0;
    std::uint16_t destinationPort_ = 0;
    std::uint16_t length_ = 0;
    std::uint16_t checksum_ = 0;
    std::vector<unsigned char> payload_;
};