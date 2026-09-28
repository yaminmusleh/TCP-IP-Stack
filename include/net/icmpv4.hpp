#pragma once

#include <cstdint>
#include <vector>

class Icmpv4Packet
{
public:
    static Icmpv4Packet parse(const std::vector<unsigned char> &data);

    std::vector<unsigned char> serialize() const;

    // getters
    std::uint8_t type() const;
    std::uint8_t code() const;
    std::uint16_t checksum() const;
    std::vector<unsigned char> &payload() const;

    // setters
private:
    std::uint8_t type_ = 0;
    std::uint8_t code_ = 0;
    std::uint16_t checksum_ = 0;
    std::vector<unsigned char> payload_;
};