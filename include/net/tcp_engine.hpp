#pragma once

#include "net/tcp.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <tuple>
#include <vector>

enum class TcpState
{
    SynReceived,
    Established
};

struct TcpOutgoing
{
    std::uint32_t remoteIp;
    TcpSegment segment;
};

class TcpEngine
{
public:
    static constexpr std::size_t MAX_CONNECTIONS = 64;
    static constexpr std::uint16_t RECEIVE_WINDOW = 8192;
    static constexpr std::uint16_t LOCAL_MSS = 1460;

    void listen(std::uint16_t port);

    // Process one incoming (checksum-verified) segment; returns segments to transmit.
    std::vector<TcpOutgoing> handleSegment(std::uint32_t remoteIp, const TcpSegment &segment);

    std::size_t connectionCount() const;

    std::optional<TcpState> stateOf(std::uint32_t remoteIp, std::uint16_t remotePort, std::uint16_t localPort) const;

private:
    using Key = std::tuple<std::uint32_t, std::uint16_t, std::uint16_t>;
    // the line above stores: remote ip, remote port, and local port : heterogeneous values (elements of different data types) into one object called Key

    struct Connection
    {
        TcpState state = TcpState::SynReceived;
        std::uint32_t iss = 0;    // our initial sequence number
        std::uint32_t irs = 0;    // peer's initial sequence number
        std::uint32_t sndUna = 0; // oldest unacknowledged byte we sent
        std::uint32_t sndNxt = 0; // next sequence number we will send
        std::uint32_t rcvNxt = 0; // next sequence number we expect from the peer
        std::optional<std::uint16_t> peerMss;
    };
    static TcpSegment makeSynAck(std::uint16_t localPort, std::uint16_t remotePort, const Connection &connection);

    std::set<std::uint16_t> listeners_;
    std::map<Key, Connection> connections_;
    std::mt19937 rng_{std::random_device{}()};
    // This line of code initializes a high-quality pseudo-random number generator (PRNG) named rng_ and seeds it using a non-deterministic hardware entropy source.
};