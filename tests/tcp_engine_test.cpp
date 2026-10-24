#include "net/tcp_engine.hpp"
#include <cassert>
#include <iostream>

static TcpSegment seg(std::uint16_t sp, std::uint16_t dp, std::uint32_t seq,
                      std::uint32_t ack, std::uint8_t flags)
{
    return TcpSegment(sp, dp, seq, ack, flags, 1000);
}

int main()
{
    const std::uint32_t R = 0x0A000002; // remote 10.0.0.2
    const std::uint8_t SYN = TcpSegment::SYN, ACK = TcpSegment::ACK,
                       RST = TcpSegment::RST, FIN = TcpSegment::FIN;

    TcpEngine e;

    // closed port: SYN -> RST+ACK (seq 0, ack = seq + 1)
    auto out = e.handleSegment(R, seg(40000, 9000, 1000, 0, SYN));
    assert(out.size() == 1);
    assert(out[0].remoteIp == R);
    assert(out[0].segment.hasFlag(RST) && out[0].segment.hasFlag(ACK));
    assert(out[0].segment.sequenceNumber() == 0);
    assert(out[0].segment.acknowledgmentNumber() == 1001);
    assert(out[0].segment.sourcePort() == 9000 && out[0].segment.destinationPort() == 40000);
    assert(e.connectionCount() == 0);

    e.listen(9000);

    // a RST, or a segment with neither SYN nor ACK, to a listener gets no reply
    assert(e.handleSegment(R, seg(40000, 9000, 1, 0, RST)).empty());
    assert(e.handleSegment(R, seg(40000, 9000, 1, 0, FIN)).empty());

    // ACK to a listener with no connection -> RST with seq = their ack
    out = e.handleSegment(R, seg(40001, 9000, 5, 777, ACK));
    assert(out.size() == 1);
    assert(out[0].segment.hasFlag(RST) && !out[0].segment.hasFlag(ACK));
    assert(out[0].segment.sequenceNumber() == 777);

    // SYN with sequence number wraparound: irs = 0xFFFFFFFF -> ack 0
    out = e.handleSegment(R, seg(40000, 9000, 0xFFFFFFFFu, 0, SYN));
    assert(out.size() == 1);
    assert(out[0].segment.hasFlag(SYN) && out[0].segment.hasFlag(ACK));
    assert(out[0].segment.acknowledgmentNumber() == 0);
    assert(out[0].segment.mss().has_value() && *out[0].segment.mss() == TcpEngine::LOCAL_MSS);
    const std::uint32_t iss = out[0].segment.sequenceNumber();
    assert(e.connectionCount() == 1);
    assert(*e.stateOf(R, 40000, 9000) == TcpState::SynReceived);

    // retransmitted SYN -> same SYN+ACK
    out = e.handleSegment(R, seg(40000, 9000, 0xFFFFFFFFu, 0, SYN));
    assert(out.size() == 1);
    assert(out[0].segment.sequenceNumber() == iss);
    assert(e.connectionCount() == 1);

    // wrong ACK number -> RST with seq = that ack, still SynReceived
    out = e.handleSegment(R, seg(40000, 9000, 0, iss + 5, ACK));
    assert(out.size() == 1);
    assert(out[0].segment.hasFlag(RST));
    assert(out[0].segment.sequenceNumber() == iss + 5);
    assert(*e.stateOf(R, 40000, 9000) == TcpState::SynReceived);

    // correct ACK -> Established, no output
    out = e.handleSegment(R, seg(40000, 9000, 0, iss + 1, ACK));
    assert(out.empty());
    assert(*e.stateOf(R, 40000, 9000) == TcpState::Established);

    // SYN on an established connection -> challenge ACK
    out = e.handleSegment(R, seg(40000, 9000, 0, 0, SYN));
    assert(out.size() == 1);
    assert(out[0].segment.hasFlag(ACK) && !out[0].segment.hasFlag(SYN));
    assert(out[0].segment.sequenceNumber() == iss + 1);
    assert(out[0].segment.acknowledgmentNumber() == 0);

    // RST with the wrong sequence number is ignored; exact match removes the connection
    assert(e.handleSegment(R, seg(40000, 9000, 77, 0, RST)).empty());
    assert(e.connectionCount() == 1);
    assert(e.handleSegment(R, seg(40000, 9000, 0, 0, RST)).empty());
    assert(e.connectionCount() == 0);
    assert(!e.stateOf(R, 40000, 9000).has_value());

    // connection cap
    TcpEngine capped;
    capped.listen(9000);
    for (std::uint16_t i = 0; i < TcpEngine::MAX_CONNECTIONS; ++i)
    {
        assert(capped.handleSegment(R, seg(10000 + i, 9000, 1, 0, SYN)).size() == 1);
    }
    assert(capped.connectionCount() == TcpEngine::MAX_CONNECTIONS);
    assert(capped.handleSegment(R, seg(20000, 9000, 1, 0, SYN)).empty());
    assert(capped.connectionCount() == TcpEngine::MAX_CONNECTIONS);

    std::cout << "TCP engine test passed!\n";
    return 0;
}