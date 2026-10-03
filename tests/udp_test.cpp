#include "net/udp.hpp"

#include <cassert>
#include <iostream>
#include <vector>

int main()
{
    std::vector<unsigned char> raw = {
        0x04, 0xD2, // source port: 1234
        0x16, 0x2E, // destination port: 5678
        0x00, 0x0D, // length: 13 (8 + 5)
        0x00, 0x00, // checksum
        'h', 'e', 'l', 'l', 'o'};

    UdpPacket packet =
        UdpPacket::parse(raw);

    assert(packet.sourcePort() == 1234);
    assert(packet.destinationPort() == 5678);
    assert(packet.length() == 13);
    assert(packet.checksum() == 0);

    const auto &payload = packet.payload();

    assert(payload.size() == 5);
    assert(payload[0] == 'h');
    assert(payload[1] == 'e');
    assert(payload[2] == 'l');
    assert(payload[3] == 'l');
    assert(payload[4] == 'o');

    auto serialized = packet.serialize();

    assert(serialized == raw);

    std::cout << "UDP round-trip test passed!\n";

    return 0;
}