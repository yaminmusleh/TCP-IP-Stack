#include "net/tcp_engine.hpp"

#include <iostream>

namespace
{
    // RFC 9293 reset generation for a segment that matches no connection.
    TcpSegment makeReset(const TcpSegment &in)
    {
        if (in.hasFlag(TcpSegment::ACK))
        {
            return TcpSegment(in.destinationPort(), in.sourcePort(),
                              in.acknowledgmentNumber(), 0,
                              TcpSegment::RST, 0);
        }

        return TcpSegment(in.destinationPort(), in.sourcePort(),
                          0, in.sequenceNumber() + in.sequenceLength(),
                          static_cast<std::uint8_t>(TcpSegment::RST | TcpSegment::ACK), 0);
    }
}

void TcpEngine::listen(std::uint16_t port)
{
    listeners_.insert(port);
}

std::size_t TcpEngine::connectionCount() const
{
    return connections_.size();
}