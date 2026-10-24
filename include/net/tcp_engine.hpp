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

    // Called with in-order received bytes; the returned bytes are queued for sending back.
    using DataHandler = std::function<std::vector<unsigned char>(
        std::uint16_t localPort, const std::vector<unsigned char> &data)>;

    void listen(std::uint16_t port);
    void setDataHandler(DataHandler handler);

    // Process one incoming (checksum-verified) segment; returns segments to transmit.
    std::vector<TcpOutgoing> handleSegment(std::uint32_t remoteIp, const TcpSegment &segment);

    std::size_t connectionCount() const;
    std::optional<TcpState> stateOf(std::uint32_t remoteIp,
                                    std::uint16_t remotePort,
                                    std::uint16_t localPort) const;

private:
    using Key = std::tuple<std::uint32_t, std::uint16_t, std::uint16_t>; // remote ip, remote port, local port

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
    };

    static TcpSegment makeSynAck(std::uint16_t localPort, std::uint16_t remotePort,
                                 const Connection &connection);
    static TcpSegment makeAck(std::uint16_t localPort, std::uint16_t remotePort,
                              const Connection &connection);

    // Sends queued data (within the peer window and MSS) and our FIN when due.
    // Returns true if at least one segment was produced.
    bool flush(const Key &key, Connection &connection, std::vector<TcpOutgoing> &out);

    std::set<std::uint16_t> listeners_;
    std::map<Key, Connection> connections_;
    DataHandler dataHandler_;
    std::mt19937 rng_{std::random_device{}()};
};