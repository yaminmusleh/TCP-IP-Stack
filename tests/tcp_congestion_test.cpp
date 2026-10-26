#include "net/tcp_engine.hpp"
#include <cassert>
#include <iostream>
#include <optional>
#include <string>

static const std::uint32_t R = 0x0A000002; // remote 10.0.0.2

static const std::uint8_t SYN = TcpSegment::SYN, ACK = TcpSegment::ACK, PSH = TcpSegment::PSH;

static std::vector<unsigned char> bytes(const std::string &s)
{
    return std::vector<unsigned char>(s.begin(), s.end());
}

static TcpSegment seg(std::uint16_t sp, std::uint16_t dp, std::uint32_t seq,
                      std::uint32_t ack, std::uint8_t flags,
                      const std::vector<unsigned char> &payload = {},
                      std::uint16_t window = 65535)
{
    return TcpSegment(sp, dp, seq, ack, flags, window, payload);
}

static std::vector<unsigned char> echoHandler(std::uint16_t, const std::vector<unsigned char> &data)
{
    return data;
}

// SYN at synTime, final ACK at ackTime (RTT = difference). Both sides advertise 65535.
static std::uint32_t establish(TcpEngine &e, std::uint16_t clientPort, std::uint32_t irs,
                               std::uint64_t synTime, std::uint64_t ackTime,
                               std::optional<std::uint16_t> mss = std::nullopt)
{
    e.advanceTime(synTime);

    TcpSegment syn(clientPort, 9000, irs, 0, SYN, 65535);
    if (mss)
    {
        syn.setMss(*mss);
    }

    auto out = e.handleSegment(R, syn);
    assert(out.size() == 1);
    const std::uint32_t iss = out[0].segment.sequenceNumber();

    e.advanceTime(ackTime);
    out = e.handleSegment(R, seg(clientPort, 9000, irs + 1, iss + 1, ACK));
    assert(out.empty());

    return iss;
}

// Engine with MSS 100, peer sent 5000 bytes at t=100; the echo of the first 1000 is in flight
// (initial window 10 * 100). Returns our ISS. RTO is 300 ms. The peer's next sequence is 6001.
static std::uint32_t startBulkTransfer(TcpEngine &e)
{
    e.listen(9000);
    e.setDataHandler(echoHandler);

    const std::uint32_t iss = establish(e, 40000, 1000, 0, 100, std::uint16_t(100));

    auto out = e.handleSegment(R, seg(40000, 9000, 1001, iss + 1, ACK | PSH,
                                      std::vector<unsigned char>(5000, 'a')));
    assert(out.size() == 10);

    return iss;
}

int main()
{
    // ===== slow start: the window grows by one MSS per ACK =====
    {
        TcpEngine e;
        e.listen(9000);
        e.setDataHandler(echoHandler);

        const std::uint32_t iss = establish(e, 40000, 1000, 0, 100, std::uint16_t(100));
        assert(*e.congestionWindow(R, 40000, 9000) == 1000); // IW = min(10*MSS, max(2*MSS, 14600))
        assert(*e.slowStartThreshold(R, 40000, 9000) == 0xFFFFFFFFu);

        auto out = e.handleSegment(R, seg(40000, 9000, 1001, iss + 1, ACK | PSH,
                                          std::vector<unsigned char>(5000, 'a')));
        assert(out.size() == 10); // only 1000 bytes may be in flight although 5000 are queued
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            assert(out[i].segment.payload().size() == 100);
            assert(out[i].segment.sequenceNumber() == iss + 1 + 100 * i);
        }

        // ACK of 100 bytes: cwnd 1000 -> 1100; 900 in flight, so two more segments fit
        out = e.handleSegment(R, seg(40000, 9000, 6001, iss + 101, ACK));
        assert(*e.congestionWindow(R, 40000, 9000) == 1100);
        assert(out.size() == 2);
        assert(out[0].segment.sequenceNumber() == iss + 1001);
        assert(out[1].segment.sequenceNumber() == iss + 1101);

        // ACK of 900 bytes at once still only grows cwnd by one MSS
        out = e.handleSegment(R, seg(40000, 9000, 6001, iss + 1001, ACK));
        assert(*e.congestionWindow(R, 40000, 9000) == 1200);
        assert(out.size() == 10);
    }

    // ===== timeout: ssthresh = flight/2, cwnd = 1 MSS; then slow start, then congestion avoidance =====
    {
        TcpEngine e;
        const std::uint32_t iss = startBulkTransfer(e);

        auto out = e.tick(400); // RTO is 300 ms, data was sent at t=100
        assert(out.size() == 1);
        assert(out[0].segment.sequenceNumber() == iss + 1);
        assert(*e.slowStartThreshold(R, 40000, 9000) == 500); // max(1000 / 2, 2 * MSS)
        assert(*e.congestionWindow(R, 40000, 9000) == 100);

        // slow start until cwnd reaches ssthresh
        for (std::uint32_t k = 1; k <= 4; ++k)
        {
            out = e.handleSegment(R, seg(40000, 9000, 6001, iss + 1 + 100 * k, ACK));
            assert(*e.congestionWindow(R, 40000, 9000) == 100 + 100 * k);
            assert(out.empty()); // still more in flight than the window allows
        }

        // congestion avoidance: cwnd += MSS*MSS/cwnd (10000 / 500 = 20)
        out = e.handleSegment(R, seg(40000, 9000, 6001, iss + 501, ACK));
        assert(*e.congestionWindow(R, 40000, 9000) == 520);
        assert(out.empty()); // only 20 bytes of window left: no sub-MSS segment is sent

        out = e.handleSegment(R, seg(40000, 9000, 6001, iss + 601, ACK)); // 10000 / 520 = 19
        assert(*e.congestionWindow(R, 40000, 9000) == 539);
        assert(out.size() == 1);
        assert(out[0].segment.sequenceNumber() == iss + 1001);
        assert(out[0].segment.payload().size() == 100);
    }

    // ===== fast retransmit and fast recovery (RFC 5681 / RFC 6582) =====
    {
        TcpEngine e;
        const std::uint32_t iss = startBulkTransfer(e);

        // the first segment is lost; the peer keeps ACKing iss+1
        assert(e.handleSegment(R, seg(40000, 9000, 6001, iss + 1, ACK)).empty());
        assert(e.handleSegment(R, seg(40000, 9000, 6001, iss + 1, ACK)).empty());

        auto out = e.handleSegment(R, seg(40000, 9000, 6001, iss + 1, ACK)); // third duplicate
        assert(out.size() == 1);
        assert(out[0].segment.sequenceNumber() == iss + 1);
        assert(out[0].segment.payload().size() == 100);
        assert(*e.slowStartThreshold(R, 40000, 9000) == 500);
        assert(*e.congestionWindow(R, 40000, 9000) == 800); // ssthresh + 3 * MSS

        // further duplicates inflate the window; once it exceeds the flight, new data goes out
        assert(e.handleSegment(R, seg(40000, 9000, 6001, iss + 1, ACK)).empty()); // 900
        assert(e.handleSegment(R, seg(40000, 9000, 6001, iss + 1, ACK)).empty()); // 1000
        out = e.handleSegment(R, seg(40000, 9000, 6001, iss + 1, ACK));           // 1100
        assert(*e.congestionWindow(R, 40000, 9000) == 1100);
        assert(out.size() == 1);
        assert(out[0].segment.sequenceNumber() == iss + 1001);

        // full ACK ends recovery: cwnd deflates to ssthresh
        out = e.handleSegment(R, seg(40000, 9000, 6001, iss + 1101, ACK));
        assert(*e.congestionWindow(R, 40000, 9000) == 500);
        assert(out.size() == 5);
    }

    // ===== partial ACK during recovery retransmits the next hole =====
    {
        TcpEngine e;
        const std::uint32_t iss = startBulkTransfer(e);

        for (int i = 0; i < 3; ++i)
        {
            e.handleSegment(R, seg(40000, 9000, 6001, iss + 1, ACK));
        }
        assert(*e.congestionWindow(R, 40000, 9000) == 800);

        auto out = e.handleSegment(R, seg(40000, 9000, 6001, iss + 301, ACK)); // below recovery point
        assert(*e.congestionWindow(R, 40000, 9000) == 600); // 800 - 300 + MSS
        assert(out.size() == 1);
        assert(out[0].segment.sequenceNumber() == iss + 301); // next hole
        assert(out[0].segment.payload().size() == 100);
    }

    // ===== zero-window probes: no deadlock, no abort, no congestion collapse =====
    {
        TcpEngine e;
        e.listen(9000);
        e.setDataHandler(echoHandler);

        const std::uint32_t iss = establish(e, 40000, 1000, 0, 100, std::uint16_t(100)); // RTO 300 ms

        std::string text;
        for (int i = 0; i < 300; ++i)
        {
            text.push_back(static_cast<char>('a' + (i % 26)));
        }

        // the peer's data segment advertises a zero window, so the echo cannot be sent
        auto out = e.handleSegment(R, seg(40000, 9000, 1001, iss + 1, ACK | PSH, bytes(text), 0));
        assert(out.size() == 1 && out[0].segment.payload().empty()); // only the ACK
        assert(out[0].segment.acknowledgmentNumber() == 1301);

        assert(e.tick(399).empty());
        out = e.tick(400); // persist timer fires: one byte
        assert(out.size() == 1);
        assert(out[0].segment.sequenceNumber() == iss + 1);
        assert(out[0].segment.payload() == bytes("a"));

        out = e.tick(700); // the probe is retransmitted by the normal timer
        assert(out.size() == 1 && out[0].segment.payload() == bytes("a"));

        // the peer answers the probe with the window still closed
        assert(e.handleSegment(R, seg(40000, 9000, 1301, iss + 1, ACK, {}, 0)).empty());

        std::uint64_t t = 700;
        for (int i = 0; i < 20; ++i) // far beyond MAX_DATA_RETRANSMITS
        {
            t += 70000;
            out = e.tick(t);
            assert(out.size() == 1 && out[0].segment.payload() == bytes("a"));
        }
        assert(e.connectionCount() == 1);
        assert(*e.congestionWindow(R, 40000, 9000) == 1000); // probes are not congestion

        // the peer finally accepts the probe byte and opens its window
        out = e.handleSegment(R, seg(40000, 9000, 1301, iss + 2, ACK, {}, 65535));
        assert(out.size() == 3);
        assert(out[0].segment.sequenceNumber() == iss + 2 && out[0].segment.payload().size() == 100);
        assert(out[1].segment.sequenceNumber() == iss + 102 && out[1].segment.payload().size() == 100);
        assert(out[2].segment.sequenceNumber() == iss + 202 && out[2].segment.payload().size() == 99);
    }

    // ===== receive window: shrinks as the echo queue fills, closes, and re-opens =====
    {
        TcpEngine e;
        e.listen(9000);
        e.setDataHandler(echoHandler);

        const std::uint32_t iss = establish(e, 40000, 1000, 0, 100);

        std::uint32_t seq = 1001;

        // the peer advertises a zero window, so nothing we echo can leave the queue
        for (int k = 1; k <= 8; ++k)
        {
            auto out = e.handleSegment(R, seg(40000, 9000, seq, iss + 1, ACK | PSH,
                                              std::vector<unsigned char>(8000, 'x'), 0));
            seq += 8000;
            assert(out.size() == 1 && out[0].segment.payload().empty());
            assert(out[0].segment.acknowledgmentNumber() == 1001 + 8000u * k);
            assert(out[0].segment.window() == (k < 8 ? 8192 : 1536));
        }

        // only 1536 bytes of the ninth segment fit; the advertised window drops to zero
        auto out = e.handleSegment(R, seg(40000, 9000, seq, iss + 1, ACK | PSH,
                                          std::vector<unsigned char>(8000, 'x'), 0));
        seq += 8000;
        assert(out.size() == 1);
        assert(out[0].segment.acknowledgmentNumber() == 1001 + 65536);
        assert(out[0].segment.window() == 0);

        // the peer retransmits the rest; with a zero window nothing more is accepted
        out = e.handleSegment(R, seg(40000, 9000, 1001 + 65536, iss + 1, ACK | PSH,
                                     std::vector<unsigned char>(6464, 'x'), 0));
        assert(out.size() == 1);
        assert(out[0].segment.acknowledgmentNumber() == 1001 + 65536);
        assert(out[0].segment.window() == 0);

        // the peer opens its window: the queue drains (limited by cwnd = 10 * 536) and so does ours
        out = e.handleSegment(R, seg(40000, 9000, seq, iss + 1, ACK, {}, 65535));
        assert(out.size() == 10);
        assert(out.back().segment.window() == 5360);
    }

    // ===== a peer that announces MSS 0 must not hang the sender =====
    {
        TcpEngine e;
        e.listen(9000);
        e.setDataHandler(echoHandler);

        const std::uint32_t iss = establish(e, 40000, 1000, 0, 100, std::uint16_t(0));

        auto out = e.handleSegment(R, seg(40000, 9000, 1001, iss + 1, ACK | PSH, bytes("ab")));
        assert(out.size() == 2);
        assert(out[0].segment.payload() == bytes("a") && out[1].segment.payload() == bytes("b"));
    }

    std::cout << "TCP congestion test passed!\n";
    return 0;
}