#pragma once

#include "net/tcp.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <tuple>
#include <vector>

enum class TcpState
{
    SynReceived,
    Established,
    CloseWait,
    LastAck
};

// A segment the engine wants sent to remoteIp. The caller adds the IPv4 header and checksum.
struct TcpOutgoing
{
    std::uint32_t remoteIp;
    TcpSegment segment;
};

class TcpEngine
{
public:
    static constexpr std::size_t MAX_CONNECTIONS = 64;
    static constexpr std::size_t MAX_SEND_QUEUE = 65536;
    static constexpr std::uint16_t RECEIVE_WINDOW = 8192;
    static constexpr std::uint16_t LOCAL_MSS = 1460;
    static constexpr std::uint16_t DEFAULT_PEER_MSS = 536;

    // Retransmission timer (RFC 6298), in milliseconds.
    static constexpr std::uint64_t INITIAL_RTO_MS = 1000;
    static constexpr std::uint64_t MIN_RTO_MS = 200;
    static constexpr std::uint64_t MAX_RTO_MS = 60000;
    static constexpr unsigned MAX_SYN_RETRANSMITS = 5;
    static constexpr unsigned MAX_DATA_RETRANSMITS = 8;

    // Called with in-order received bytes; the returned bytes are queued for sending back.
    using DataHandler = std::function<std::vector<unsigned char>(
        std::uint16_t localPort, const std::vector<unsigned char> &data)>;

    void listen(std::uint16_t port);
    void setDataHandler(DataHandler handler);

    // Process one incoming (checksum-verified) segment; returns segments to transmit.
    std::vector<TcpOutgoing> handleSegment(std::uint32_t remoteIp, const TcpSegment &segment);

    void advanceTime(std::uint64_t nowMs);
    std::vector<TcpOutgoing> tick(std::uint64_t nowMs);

    std::size_t connectionCount() const;
    std::optional<TcpState> stateOf(std::uint32_t remoteIp,
                                    std::uint16_t remotePort,
                                    std::uint16_t localPort) const;

private:
    using Key = std::tuple<std::uint32_t, std::uint16_t, std::uint16_t>; // remote ip, remote port, local port

    struct SentSegment
    {
        std::uint32_t seq;
        std::vector<unsigned char> data;
        bool syn;
        bool fin;
        std::uint64_t sentAt;
        bool retransmitted;
    };

    struct Connection
    {
        TcpState state = TcpState::SynReceived;
        std::uint32_t iss = 0;    // our initial sequence number
        std::uint32_t irs = 0;    // peer's initial sequence number
        std::uint32_t sndUna = 0; // oldest unacknowledged sequence number we sent
        std::uint32_t sndNxt = 0; // next sequence number we will send
        std::uint32_t rcvNxt = 0; // next sequence number we expect from the peer
        std::uint32_t sndWnd = 0; // peer's advertised window
        std::uint32_t sndWl1 = 0; // segment seq used for the last window update
        std::uint32_t sndWl2 = 0; // segment ack used for the last window update
        std::optional<std::uint16_t> peerMss;
        std::deque<unsigned char> sendQueue; // bytes waiting for window space
        bool finPending = false;             // peer closed; send our FIN once the queue drains
        bool finSent = false;

        std::deque<SentSegment> unacked;
        
        std::optional<std::uint64_t> rtoDeadline; // when the oldest unacked segment times out

        std::uint64_t rto = INITIAL_RTO_MS; // rto is the time TCP waits for an ACK before retransmitting a lost packet

        std::optional<std::uint64_t> srtt; // Smoothed Round-Trip Time,
        // It represents a running, filtered average of the time it takes for a data packet to travel from the sender to the receiver
        // and for the acknowledgment (ACK) to return.

        std::uint64_t rttvar = 0; // rtt variance
        // rtt Variance: a metric in the TCP/IP stack that measures how much the Round-Trip Time (RTT) fluctuates or "jitters" over a network connection

        unsigned retransmitCount = 0;
    };

    static TcpSegment makeSynAck(std::uint16_t localPort, std::uint16_t remotePort,
                                 const Connection &connection);
    static TcpSegment makeAck(std::uint16_t localPort, std::uint16_t remotePort,
                              const Connection &connection);
    static TcpSegment makeRetransmission(const Key &key, const Connection &connection,
                                         const SentSegment &sent);

    // Sends queued data (within the peer window and MSS) and our FIN when due.
    // Returns true if at least one segment was produced.
    bool flush(const Key &key, Connection &connection, std::vector<TcpOutgoing> &out);

    void onAck(Connection &connection, std::uint32_t ack);
    void sampleRtt(Connection &connection, std::uint64_t rttMs);
    void armTimer(Connection &connection);

    std::set<std::uint16_t> listeners_;
    std::map<Key, Connection> connections_;
    DataHandler dataHandler_;
    std::uint64_t now_ = 0;
    std::mt19937 rng_{std::random_device{}()};
};