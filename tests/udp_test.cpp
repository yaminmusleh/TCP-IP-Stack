#include "net/udp.hpp"
#include <cstdint>
#include <stdexcept>
#include <cassert>
#include <iostream>
#include <vector>

int main()
{
    std::vector<unsigned char> payload = {
        'h', 'e', 'l', 'l', 'o'};

    UdpPacket packet(
        1234,
        5678,
        payload);

    assert(packet.sourcePort() == 1234);
    assert(packet.destinationPort() == 5678);
    assert(packet.length() == 13);
    assert(packet.checksum() == 0);
    assert(packet.payload() == payload);

    std::vector<unsigned char> raw = {
        0x04, 0xD2, // source port: 1234
        0x16, 0x2E, // destination port: 5678
        0x00, 0x0D, // length: 13 (8 + 5)
        0x00, 0x00, // checksum
        'h', 'e', 'l', 'l', 'o'};

    UdpPacket parsedPacket =
        UdpPacket::parse(raw);

    assert(parsedPacket.sourcePort() == 1234);
    assert(parsedPacket.destinationPort() == 5678);
    assert(parsedPacket.length() == 13);
    assert(parsedPacket.checksum() == 0);

    const auto &parsedPayload = parsedPacket.payload();

    assert(parsedPayload.size() == 5);
    assert(parsedPayload[0] == 'h');
    assert(parsedPayload[1] == 'e');
    assert(parsedPayload[2] == 'l');
    assert(parsedPayload[3] == 'l');
    assert(parsedPayload[4] == 'o');

    auto serialized = parsedPacket.serialize();

    assert(serialized == raw);

    const std::uint32_t sourceIp = 0x0A000001;      // 10.0.0.1
    const std::uint32_t destinationIp = 0x0A000002; // 10.0.0.2

    UdpPacket checksummedPacket(1234, 5678, payload);
    checksummedPacket.setChecksum(sourceIp, destinationIp);

    assert(checksummedPacket.checksum() != 0);
    assert(checksummedPacket.verifyChecksum(sourceIp, destinationIp));

    // Parse a packet with its checksum and verify it again
    auto checksummedBytes = checksummedPacket.serialize();
    auto parsedChecksummedPacket = UdpPacket::parse(checksummedBytes);

    assert(parsedChecksummedPacket.verifyChecksum(sourceIp, destinationIp));

    // Corrupt the payload and ensure checksum verification fails
    checksummedBytes.back() ^= 0x01;
    auto corruptedPacket = UdpPacket::parse(checksummedBytes);

    assert(!corruptedPacket.verifyChecksum(sourceIp, destinationIp));

    // Reject a packet shorter than the UDP header
    bool shortPacketRejected = false;

    try
    {
        UdpPacket::parse({0x00, 0x01, 0x00});
    }
    catch (const std::runtime_error &)
    {
        shortPacketRejected = true;
    }

    assert(shortPacketRejected);

    // Reject a packet whose declared length doesn't match its data
    bool invalidLengthRejected = false;

    try
    {
        auto invalidPacket = raw;
        invalidPacket[5] = 0x0E; // Claims length 14, but data is 13 bytes
        UdpPacket::parse(invalidPacket);
    }
    catch (const std::runtime_error &)
    {
        invalidLengthRejected = true;
    }

    assert(invalidLengthRejected);

    std::cout << "UDP round-trip test passed!\n";

    return 0;
}