#include "net/udp.hpp"

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

    std::cout << "UDP round-trip test passed!\n";

    return 0;
}