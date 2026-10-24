#include "net/tcp.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

// Independent reference: sum 16-bit big-endian words, fold carries, complement.
static std::uint16_t referenceChecksum(const std::vector<unsigned char> &bytes)
{
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < bytes.size(); i += 2)
    {
        std::uint32_t word = static_cast<std::uint32_t>(bytes[i]) << 8;
        if (i + 1 < bytes.size())
        {
            word |= bytes[i + 1];
        }
        sum += word;
    }
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return static_cast<std::uint16_t>(~sum);
}

int main()
{
    const std::uint32_t srcIp = 0x0A000001; // 10.0.0.1
    const std::uint32_t dstIp = 0x0A000002; // 10.0.0.2

    // --- build, serialize, parse round trip (with MSS option) ---
    TcpSegment syn(40000, 9000, 0xFFFFFFF0u, 0, TcpSegment::SYN, 65535);
    syn.setMss(1460);
    syn.setChecksum(srcIp, dstIp);

    auto bytes = syn.serialize();
    assert(bytes.size() == 24);       // 20 + 4 option bytes
    assert((bytes[12] >> 4) == 6);    // data offset: 6 words

    TcpSegment parsed = TcpSegment::parse(bytes);
    assert(parsed.sourcePort() == 40000);
    assert(parsed.destinationPort() == 9000);
    assert(parsed.sequenceNumber() == 0xFFFFFFF0u);
    assert(parsed.acknowledgmentNumber() == 0);
    assert(parsed.hasFlag(TcpSegment::SYN));
    assert(!parsed.hasFlag(TcpSegment::ACK));
    assert(parsed.window() == 65535);
    assert(parsed.mss().has_value() && *parsed.mss() == 1460);
    assert(parsed.payload().empty());
    assert(parsed.headerLength() == 24);
    assert(parsed.sequenceLength() == 1); // SYN consumes one sequence number
    assert(parsed.verifyChecksum(srcIp, dstIp));
    assert(parsed.serialize() == bytes);

    // --- checksum matches an independent calculation (odd payload length) ---
    std::vector<unsigned char> payload = {'a', 'b', 'c', 'd', 'e'};
    TcpSegment data(1234, 5678, 1000, 2000, TcpSegment::ACK | TcpSegment::PSH, 8192, payload);

    std::vector<unsigned char> reference = {
        10, 0, 0, 1, 10, 0, 0, 2, 0, 6, 0, 25}; // pseudo-header, TCP length = 20 + 5
    auto noChecksum = data.serialize(); // checksum field is still 0 here
    reference.insert(reference.end(), noChecksum.begin(), noChecksum.end());

    data.setChecksum(srcIp, dstIp);
    assert(data.checksum() == referenceChecksum(reference));
    assert(data.verifyChecksum(srcIp, dstIp));
    assert(data.sequenceLength() == 5);

    // --- corruption and wrong addresses are detected ---
    auto corrupted = data.serialize();
    corrupted.back() ^= 0x01;
    assert(!TcpSegment::parse(corrupted).verifyChecksum(srcIp, dstIp));
    assert(!data.verifyChecksum(srcIp, 0x0A000003));

    // --- FIN counts as one sequence number ---
    TcpSegment fin(1, 2, 0, 0, TcpSegment::FIN | TcpSegment::ACK, 0);
    assert(fin.sequenceLength() == 1);

    // --- malformed segments are rejected ---
    auto rejects = [](std::vector<unsigned char> raw)
    {
        try
        {
            TcpSegment::parse(raw);
        }
        catch (const std::runtime_error &)
        {
            return true;
        }
        return false;
    };

    assert(rejects(std::vector<unsigned char>(19, 0)));  // shorter than minimum header

    auto badOffsetLow = bytes;
    badOffsetLow[12] = 0x40; // data offset 4 (< 5)
    assert(rejects(badOffsetLow));

    auto badOffsetHigh = std::vector<unsigned char>(20, 0);
    badOffsetHigh[12] = 0xF0; // data offset 15 = 60 bytes, but only 20 present
    assert(rejects(badOffsetHigh));

    // --- malformed option must not read out of bounds ---
    auto badOption = bytes;
    badOption[21] = 200; // MSS option length claims 200 bytes
    TcpSegment weird = TcpSegment::parse(badOption);
    assert(!weird.mss().has_value());

    // --- sequence-number wraparound ---
    assert(seqLt(0xFFFFFFF0u, 5u));
    assert(seqGt(5u, 0xFFFFFFF0u));
    assert(seqLeq(7u, 7u) && seqGeq(7u, 7u));
    assert(!seqLt(7u, 7u));

    std::cout << "TCP segment test passed!\n";
    return 0;
}