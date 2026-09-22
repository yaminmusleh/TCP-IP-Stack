#pragma once
#include <vector>
#include <cstdint>
class Ipv4Packet
{
public:
    static Ipv4Packet parse(
        const std::vector<unsigned char> &data);

    std::vector<unsigned char> serialize() const;

    std::uint8_t version() const;
    std::uint8_t headerLength() const;
    std::uint8_t tos() const;

    std::uint16_t totalLength() const;
    std::uint16_t identification() const;

    std::uint8_t flags() const;
    std::uint16_t fragmentOffset() const;

    std::uint8_t ttl() const;
    std::uint8_t protocol() const;

    std::uint16_t checksum() const;

    std::uint32_t sourceIp() const;
    std::uint32_t destinationIp() const;

    const std::vector<unsigned char> &payload() const;

private:
    std::uint8_t version_ = 0;
    std::uint8_t header_length_ = 0;
    std::uint8_t tos_ = 0; // tos stands for type of service

    std::uint16_t total_length_ = 0;
    std::uint16_t identification_ = 0;

    std::uint8_t flags_ = 0;            // this is 3 bits long so we use the one that stores 8 bit
    std::uint16_t fragment_offset_ = 0; // this is 13 bit long so we use uint16_t

    std::uint8_t ttl_ = 0; // ttl stands for time to live
    std::uint8_t protocol_ = 0;

    std::uint16_t checksum_ = 0;

    std::uint32_t source_ip_ = 0;
    std::uint32_t destination_ip_ = 0;

    std::vector<unsigned char> payload_;
};