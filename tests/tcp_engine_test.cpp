#include "net/tcp_engine.hpp"
#include <cassert>
#include <iostream>
#include <optional>
#include <string>

static const std::uint32_t R = 0x0A000002; // remote 10.0.0.2

static const std::uint8_t SYN = TcpSegment::SYN, ACK = TcpSegment::ACK,
                          RST = TcpSegment::RST, FIN = TcpSegment::FIN,
                          PSH = TcpSegment::PSH;

static std::vector<unsigned char> bytes(const std::string &s)
{
    return std::vector<unsigned char>(s.begin(), s.end());
}

static TcpSegment seg(std::uint16_t sp, std::uint16_t dp, std::uint32_t seq,
                      std::uint32_t ack, std::uint8_t flags,
                      std::uint16_t window = 1000,
                      const std::vector<unsigned char> &payload = {})
{
    return TcpSegment(sp, dp, seq, ack, flags, window, payload);
}

static int handlerCalls = 0;
static std::vector<unsigned char> lastData;

static std::vector<unsigned char> echoHandler(std::uint16_t, const std::vector<unsigned char> &data)
{
    ++handlerCalls;
    lastData = data;
    return data; // echo
}

// Performs SYN / SYN+ACK / ACK and returns our ISS. Client's next sequence number is irs + 1.
static std::uint32_t establish(TcpEngine &e, std::uint16_t clientPort, std::uint32_t irs,
                               std::uint16_t window = 1000,
                               std::optional<std::uint16_t> mss = std::nullopt)
{
    TcpSegment syn(clientPort, 9000, irs, 0, SYN, window);
    if (mss)
    {
        syn.setMss(*mss);
    }

    auto out = e.handleSegment(R, syn);
    assert(out.size() == 1);
    const std::uint32_t iss = out[0].segment.sequenceNumber();

    out = e.handleSegment(R, seg(clientPort, 9000, irs + 1, iss + 1, ACK, window));
    assert(out.empty());

    return iss;
}

int main()
{
    // ===== Step 2: handshake and reset behavior =====
    {
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
    }

    // ===== Step 3: data, duplicates, out-of-order, overlap, then passive close =====
    {
        TcpEngine e;
        e.listen(9000);
        e.setDataHandler(echoHandler);

        const std::uint32_t iss = establish(e, 40000, 1000); // peer's next seq = 1001

        // in-order data: echo comes back with the ACK piggybacked
        auto out = e.handleSegment(R, seg(40000, 9000, 1001, iss + 1, ACK | PSH, 1000, bytes("hello")));
        assert(out.size() == 1);
        assert(out[0].segment.payload() == bytes("hello"));
        assert(out[0].segment.sequenceNumber() == iss + 1);
        assert(out[0].segment.acknowledgmentNumber() == 1006);
        assert(out[0].segment.hasFlag(ACK) && !out[0].segment.hasFlag(SYN) && !out[0].segment.hasFlag(FIN));
        assert(handlerCalls == 1);

        // duplicate (retransmitted) data: bare ACK, handler not called again
        out = e.handleSegment(R, seg(40000, 9000, 1001, iss + 1, ACK, 1000, bytes("hello")));
        assert(out.size() == 1);
        assert(out[0].segment.payload().empty());
        assert(out[0].segment.acknowledgmentNumber() == 1006);
        assert(out[0].segment.sequenceNumber() == iss + 6);
        assert(handlerCalls == 1);

        // out-of-order (gap): dropped, duplicate ACK for what we expect
        out = e.handleSegment(R, seg(40000, 9000, 1011, iss + 6, ACK, 1000, bytes("zzz")));
        assert(out.size() == 1);
        assert(out[0].segment.payload().empty());
        assert(out[0].segment.acknowledgmentNumber() == 1006);
        assert(handlerCalls == 1);

        // peer ACKs our echo: nothing to send
        out = e.handleSegment(R, seg(40000, 9000, 1006, iss + 6, ACK));
        assert(out.empty());

        // partial overlap: first 2 bytes already received, only "ABC" is new
        out = e.handleSegment(R, seg(40000, 9000, 1004, iss + 6, ACK, 1000, bytes("loABC")));
        assert(handlerCalls == 2);
        assert(lastData == bytes("ABC"));
        assert(out.size() == 1);
        assert(out[0].segment.payload() == bytes("ABC"));
        assert(out[0].segment.sequenceNumber() == iss + 6);
        assert(out[0].segment.acknowledgmentNumber() == 1009);

        // peer FIN (also ACKing the echo): we ACK and send our FIN in one segment
        out = e.handleSegment(R, seg(40000, 9000, 1009, iss + 9, ACK | FIN));
        assert(out.size() == 1);
        assert(out[0].segment.hasFlag(FIN) && out[0].segment.hasFlag(ACK));
        assert(out[0].segment.sequenceNumber() == iss + 9);
        assert(out[0].segment.acknowledgmentNumber() == 1010);
        assert(*e.stateOf(R, 40000, 9000) == TcpState::LastAck);

        // retransmitted peer FIN: bare ACK only
        out = e.handleSegment(R, seg(40000, 9000, 1009, iss + 9, ACK | FIN));
        assert(out.size() == 1);
        assert(!out[0].segment.hasFlag(FIN));
        assert(out[0].segment.acknowledgmentNumber() == 1010);
        assert(out[0].segment.sequenceNumber() == iss + 10);

        // final ACK of our FIN: connection removed
        out = e.handleSegment(R, seg(40000, 9000, 1010, iss + 10, ACK));
        assert(out.empty());
        assert(e.connectionCount() == 0);
    }

    // ===== data and FIN in the same segment =====
    {
        TcpEngine e;
        e.listen(9000);
        e.setDataHandler(echoHandler);

        const std::uint32_t iss = establish(e, 40001, 5000);

        auto out = e.handleSegment(R, seg(40001, 9000, 5001, iss + 1, ACK | FIN, 1000, bytes("bye")));
        assert(out.size() == 2);
        assert(out[0].segment.payload() == bytes("bye"));
        assert(!out[0].segment.hasFlag(FIN));
        assert(out[0].segment.acknowledgmentNumber() == 5005);
        assert(out[1].segment.hasFlag(FIN) && out[1].segment.payload().empty());
        assert(out[1].segment.sequenceNumber() == iss + 4);
        assert(out[1].segment.acknowledgmentNumber() == 5005);
        assert(*e.stateOf(R, 40001, 9000) == TcpState::LastAck);
    }

    // ===== peer MSS limits segment size =====
    {
        TcpEngine e;
        e.listen(9000);
        e.setDataHandler(echoHandler);

        const std::uint32_t iss = establish(e, 40002, 100, 1000, std::uint16_t(4));

        auto out = e.handleSegment(R, seg(40002, 9000, 101, iss + 1, ACK, 1000, bytes("0123456789")));
        assert(out.size() == 3);
        assert(out[0].segment.payload() == bytes("0123") && out[0].segment.sequenceNumber() == iss + 1);
        assert(out[1].segment.payload() == bytes("4567") && out[1].segment.sequenceNumber() == iss + 5);
        assert(out[2].segment.payload() == bytes("89") && out[2].segment.sequenceNumber() == iss + 9);
    }

    // ===== peer window limits sending; a window update releases the rest =====
    {
        TcpEngine e;
        e.listen(9000);
        e.setDataHandler(echoHandler);

        const std::uint32_t iss = establish(e, 40003, 7, 3); // peer window = 3

        auto out = e.handleSegment(R, seg(40003, 9000, 8, iss + 1, ACK, 3, bytes("abcdefgh")));
        assert(out.size() == 1);
        assert(out[0].segment.payload() == bytes("abc"));

        // peer ACKs "abc" and opens its window to 5
        out = e.handleSegment(R, seg(40003, 9000, 16, iss + 4, ACK, 5));
        assert(out.size() == 1);
        assert(out[0].segment.payload() == bytes("defgh"));
        assert(out[0].segment.sequenceNumber() == iss + 4);
    }

    std::cout << "TCP engine test passed!\n";
    return 0;
}