#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// Sequence numbers wrap around at 2^32, so comparisons must use modular arithmetic (RFC 9293 3.4).
inline bool seqLt(std::uint32_t a, std::uint32_t b) { return static_cast<std::int32_t>(a - b) < 0; }
inline bool seqLeq(std::uint32_t a, std::uint32_t b) { return static_cast<std::int32_t>(a - b) <= 0; }
inline bool seqGt(std::uint32_t a, std::uint32_t b) { return static_cast<std::int32_t>(a - b) > 0; }
inline bool seqGeq(std::uint32_t a, std::uint32_t b) { return static_cast<std::int32_t>(a - b) >= 0; }

class TcpSegment
{
public:
    static constexpr std::uint8_t FIN = 0x01; // Finish: Closes the connection
    static constexpr std::uint8_t SYN = 0x02; // Synchronize: Used in the 3-way handshake to initiate a connection (IMPORTANT)
    static constexpr std::uint8_t RST = 0x04; // Reset: Abruptly aborts/tears down a broken connection
    static constexpr std::uint8_t PSH = 0x08; // Push: Tells receiver to pass data to the app immediately, don't buffer
    static constexpr std::uint8_t ACK = 0x10; // Acknowledgment: Indicates that the ACK field is valid
    static constexpr std::uint8_t URG = 0x20; // Urgent: Indicates urgent data needs immediate processing

    TcpSegment(std::uint16_t sourcePort,
               std::uint16_t destinationPort,
               std::uint32_t sequenceNumber,
               std::uint32_t acknowledgmentNumber,
               std::uint8_t flags,
               std::uint16_t window,
               const std::vector<unsigned char> &payload = {});

    static TcpSegment parse(const std::vector<unsigned char> &data);

    std::vector<unsigned char> serialize() const;

    std::uint16_t calcChecksum(std::uint32_t sourceIp, std::uint32_t destinationIp) const;
    void setChecksum(std::uint32_t sourceIp, std::uint32_t destinationIp);
    bool verifyChecksum(std::uint32_t sourceIp, std::uint32_t destinationIp) const;

private:
};