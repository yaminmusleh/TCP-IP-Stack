#include "net/tcp_engine.hpp"
#include <cassert>
#include <iostream>
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
                      const std::vector<unsigned char> &payload = {})
{
    return TcpSegment(sp, dp, seq, ack, flags, 1000, payload);
}

static std::vector<unsigned char> echoHandler(std::uint16_t, const std::vector<unsigned char> &data)
{
    return data;
}

// SYN at synTime, final ACK at ackTime. The measured RTT is ackTime - synTime. Returns our ISS.
static std::uint32_t establish(TcpEngine &e, std::uint16_t clientPort, std::uint32_t irs,
                               std::uint64_t synTime, std::uint64_t ackTime)
{
    e.advanceTime(synTime);
    auto out = e.handleSegment(R, seg(clientPort, 9000, irs, 0, SYN));
    assert(out.size() == 1);
    const std::uint32_t iss = out[0].segment.sequenceNumber();

    e.advanceTime(ackTime);
    out = e.handleSegment(R, seg(clientPort, 9000, irs + 1, iss + 1, ACK));
    assert(out.empty());

    return iss;
}

int main()
{
    // ===== lost SYN+ACK: retransmit with exponential backoff, then give up =====
    {
        TcpEngine e;
        e.listen(9000);

        e.advanceTime(0);
        auto out = e.handleSegment(R, seg(40000, 9000, 1000, 0, SYN));
        assert(out.size() == 1);
        const std::uint32_t iss = out[0].segment.sequenceNumber();

        assert(e.tick(999).empty()); // initial RTO is 1000 ms

        out = e.tick(1000);
        assert(out.size() == 1 && out[0].remoteIp == R);
        assert(out[0].segment.hasFlag(SYN) && out[0].segment.hasFlag(ACK));
        assert(out[0].segment.sequenceNumber() == iss);
        assert(out[0].segment.acknowledgmentNumber() == 1001);
        assert(out[0].segment.mss().has_value());

        assert(e.tick(2999).empty()); // RTO doubled to 2000 ms
        assert(e.tick(3000).size() == 1);
        assert(e.tick(7000).size() == 1);  // 4000 ms
        assert(e.tick(15000).size() == 1); // 8000 ms
        assert(e.tick(31000).size() == 1); // 16000 ms, fifth and last retransmission

        assert(e.tick(62999).empty()); // 32000 ms
        assert(e.connectionCount() == 1);

        out = e.tick(63000); // limit reached: abort with RST
        assert(out.size() == 1);
        assert(out[0].segment.hasFlag(RST));
        assert(e.connectionCount() == 0);
    }

    // ===== an acknowledged handshake leaves no timer running =====
    {
        TcpEngine e;
        e.listen(9000);
        establish(e, 40000, 1000, 0, 100);

        assert(e.tick(100000).empty());
        assert(e.connectionCount() == 1);
    }

    // ===== lost data: retransmit, Karn's rule keeps the backed-off RTO =====
    {
        TcpEngine e;
        e.listen(9000);
        e.setDataHandler(echoHandler);

        // RTT 100 ms -> srtt 100, rttvar 50, RTO = 100 + 200 = 300 ms
        const std::uint32_t iss = establish(e, 40000, 1000, 0, 100);

        auto out = e.handleSegment(R, seg(40000, 9000, 1001, iss + 1, ACK | PSH, bytes("hello")));
        assert(out.size() == 1 && out[0].segment.payload() == bytes("hello"));

        assert(e.tick(399).empty());

        out = e.tick(400);
        assert(out.size() == 1);
        assert(out[0].remoteIp == R);
        assert(out[0].segment.sequenceNumber() == iss + 1);
        assert(out[0].segment.acknowledgmentNumber() == 1006);
        assert(out[0].segment.payload() == bytes("hello"));
        assert(out[0].segment.hasFlag(ACK));

        // The ACK arrives after the retransmission: timer stops, no RTT sample is taken.
        e.advanceTime(450);
        assert(e.handleSegment(R, seg(40000, 9000, 1006, iss + 6, ACK)).empty());
        assert(e.tick(10000).empty());

        // RTO is still the backed-off 600 ms, because no fresh RTT sample was allowed.
        out = e.handleSegment(R, seg(40000, 9000, 1006, iss + 6, ACK | PSH, bytes("x")));
        assert(out.size() == 1 && out[0].segment.payload() == bytes("x"));
        assert(e.tick(10599).empty());

        out = e.tick(10600);
        assert(out.size() == 1);
        assert(out[0].segment.sequenceNumber() == iss + 6);
        assert(out[0].segment.payload() == bytes("x"));
        assert(out[0].segment.acknowledgmentNumber() == 1007);
    }

    // ===== partial ACK: only the unacknowledged tail is retransmitted =====
    {
        TcpEngine e;
        e.listen(9000);
        e.setDataHandler(echoHandler);

        const std::uint32_t iss = establish(e, 40000, 1000, 0, 100); // RTO 300 ms

        auto out = e.handleSegment(R, seg(40000, 9000, 1001, iss + 1, ACK | PSH, bytes("abcdef")));
        assert(out.size() == 1 && out[0].segment.payload() == bytes("abcdef"));

        e.advanceTime(150);
        assert(e.handleSegment(R, seg(40000, 9000, 1007, iss + 4, ACK)).empty());

        assert(e.tick(449).empty()); // timer restarted at 150 + 300
        out = e.tick(450);
        assert(out.size() == 1);
        assert(out[0].segment.sequenceNumber() == iss + 4);
        assert(out[0].segment.payload() == bytes("def"));

        e.advanceTime(500);
        assert(e.handleSegment(R, seg(40000, 9000, 1007, iss + 7, ACK)).empty());
        assert(e.tick(100000).empty());
    }

    // ===== lost FIN in LAST-ACK is retransmitted; the final ACK closes the connection =====
    {
        TcpEngine e;
        e.listen(9000);
        e.setDataHandler(echoHandler);

        const std::uint32_t iss = establish(e, 40000, 1000, 0, 100);

        auto out = e.handleSegment(R, seg(40000, 9000, 1001, iss + 1, ACK | FIN));
        assert(out.size() == 1 && out[0].segment.hasFlag(FIN));
        assert(*e.stateOf(R, 40000, 9000) == TcpState::LastAck);

        assert(e.tick(399).empty());
        out = e.tick(400);
        assert(out.size() == 1);
        assert(out[0].segment.hasFlag(FIN) && out[0].segment.hasFlag(ACK));
        assert(out[0].segment.sequenceNumber() == iss + 1);
        assert(out[0].segment.acknowledgmentNumber() == 1002);

        e.advanceTime(450);
        assert(e.handleSegment(R, seg(40000, 9000, 1002, iss + 2, ACK)).empty());
        assert(e.connectionCount() == 0);
    }

    // ===== a slow path produces a larger RTO than the minimum =====
    {
        TcpEngine e;
        e.listen(9000);
        e.setDataHandler(echoHandler);

        // RTT 1000 ms -> srtt 1000, rttvar 500, RTO = 1000 + 2000 = 3000 ms
        const std::uint32_t iss = establish(e, 40000, 1000, 0, 1000);

        auto out = e.handleSegment(R, seg(40000, 9000, 1001, iss + 1, ACK | PSH, bytes("slow")));
        assert(out.size() == 1);

        assert(e.tick(3999).empty());
        assert(e.tick(4000).size() == 1);
    }

    std::cout << "TCP retransmission test passed!\n";
    return 0;
}