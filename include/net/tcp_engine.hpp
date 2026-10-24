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
private:
    using Key = std::tuple<std::uint32_t, std::uint16_t, std::uint16_t>;
    // the line above stores: remote ip, remote port, and local port : heterogeneous values (elements of different data types) into one object called Key
};