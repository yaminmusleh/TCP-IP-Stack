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

    // Congestion control (RFC 5681 / RFC 6582 NewReno / RFC 6928 initial window).
    static constexpr unsigned DUP_ACK_THRESHOLD = 3;

    // Called with in-order received bytes; the returned bytes are queued for sending back.
    using DataHandler = std::function<std::vector<unsigned char>(
        std::uint16_t localPort, const std::vector<unsigned char> &data)>;

    void listen(std::uint16_t port);
    void setDataHandler(DataHandler handler);

    // Process one incoming (checksum-verified) segment; returns segments to transmit.
    std::vector<TcpOutgoing> handleSegment(std::uint32_t remoteIp, const TcpSegment &segment);

    // The engine never reads a clock; the caller supplies monotonic milliseconds.
    // advanceTime() only moves the clock; tick() also fires retransmission and persist timers.
    void advanceTime(std::uint64_t nowMs);
    std::vector<TcpOutgoing> tick(std::uint64_t nowMs);

    std::size_t connectionCount() const;
    std::optional<TcpState> stateOf(std::uint32_t remoteIp,
                                    std::uint16_t remotePort,
                                    std::uint16_t localPort) const;

    // Congestion state, exposed for tests and diagnostics.
    std::optional<std::uint32_t> congestionWindow(std::uint32_t remoteIp,
                                                  std::uint16_t remotePort,
                                                  std::uint16_t localPort) const;
    std::optional<std::uint32_t> slowStartThreshold(std::uint32_t remoteIp,
                                                    std::uint16_t remotePort,
                                                    std::uint16_t localPort) const;

private:
    using Key = std::tuple<std::uint32_t, std::uint16_t, std::uint16_t>; // remote ip, remote port, local port

    // One sent-but-unacknowledged unit of sequence space: our SYN, a data chunk, or our FIN.
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
        std::uint64_t rto = INITIAL_RTO_MS;
        std::optional<std::uint64_t> srtt;
        std::uint64_t rttvar = 0;
        unsigned retransmitCount = 0;

        // congestion control
        std::uint32_t cwnd = 0;
        std::uint32_t ssthresh = 0xFFFFFFFFu;
        unsigned dupAcks = 0;
        bool inRecovery = false;
        std::uint32_t recover = 0; // highest sequence number sent when loss was detected (NewReno)

        // window management
        std::optional<std::uint64_t> persistDeadline; // when to send a zero-window probe
        std::uint16_t lastAdvertised = RECEIVE_WINDOW;
    };

    static TcpSegment makeSynAck(std::uint16_t localPort, std::uint16_t remotePort,
                                 const Connection &connection);
    static TcpSegment makeAck(std::uint16_t localPort, std::uint16_t remotePort,
                              const Connection &connection);
    static TcpSegment makeRetransmission(const Key &key, const Connection &connection,
                                         const SentSegment &sent);

    static std::uint32_t smss(const Connection &connection);
    static std::uint16_t advertisedWindow(const Connection &connection);

    // Sends queued data (within the peer window, congestion window and MSS) and our FIN when due.
    // Returns true if at least one segment was produced.
    bool flush(const Key &key, Connection &connection, std::vector<TcpOutgoing> &out);

    void onAck(Connection &connection, std::uint32_t ack);
    void sampleRtt(Connection &connection, std::uint64_t rttMs);
    void armTimer(Connection &connection);

    void onNewAckCongestion(Connection &connection, std::uint32_t ack,
                            std::uint32_t bytesAcked, bool &retransmitFront);
    void onDupAck(Connection &connection, std::uint32_t ack, bool &retransmitFront);
    void onTimeoutCongestion(Connection &connection);

    std::set<std::uint16_t> listeners_;
    std::map<Key, Connection> connections_;
    DataHandler dataHandler_;
    std::uint64_t now_ = 0;
    std::mt19937 rng_{std::random_device{}()};
};