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

std::optional<TcpState> TcpEngine::stateOf(std::uint32_t remoteIp,
                                           std::uint16_t remotePort,
                                           std::uint16_t localPort) const
{
    auto it = connections_.find(Key{remoteIp, remotePort, localPort});

    if (it == connections_.end())
    {
        return std::nullopt;
    }

    return it->second.state;
}

TcpSegment TcpEngine::makeSynAck(std::uint16_t localPort, std::uint16_t remotePort, const Connection &connection)
{
    TcpSegment synAck(localPort, remotePort, connection.iss, connection.rcvNxt, static_cast<std::uint8_t>(TcpSegment::SYN | TcpSegment::ACK), RECEIVE_WINDOW);

    synAck.setMss(LOCAL_MSS);

    return synAck;
}

std::vector<TcpOutgoing> TcpEngine::handleSegment(std::uint32_t remoteIp, const TcpSegment &seg)
{
    std::vector<TcpOutgoing> out;

    const Key key{remoteIp, seg.sourcePort(), seg.destinationPort()};
    auto it = connections_.find(key);

    // ---- No matching connection ----
    if (it == connections_.end())
    {
        if (seg.hasFlag(TcpSegment::RST))
        {
            return out; // never answer a RST with a RST
        }

        const bool listening = listeners_.count(seg.destinationPort()) != 0;

        if (listening && seg.hasFlag(TcpSegment::SYN) && !seg.hasFlag(TcpSegment::ACK))
        {
            if (connections_.size() >= MAX_CONNECTIONS)
            {
                std::cout << "TCP connection table full; dropping SYN.\n";
                return out;
            }

            Connection connection;
            connection.state = TcpState::SynReceived;
            connection.irs = seg.sequenceNumber();
            connection.rcvNxt = connection.irs + 1;
            connection.iss = static_cast<std::uint32_t>(rng_());
            connection.sndUna = connection.iss;
            connection.sndNxt = connection.iss + 1; // our SYN uses one sequence number
            connection.peerMss = seg.mss();

            out.push_back(TcpOutgoing{
                remoteIp,
                makeSynAck(seg.destinationPort(), seg.sourcePort(), connection)});

            connections_.emplace(key, connection);

            std::cout << "TCP SYN accepted on port " << seg.destinationPort()
                      << "; SYN+ACK sent.\n";
            return out;
        }

        // A listening port silently drops segments with neither SYN nor ACK.
        if (listening && !seg.hasFlag(TcpSegment::SYN) && !seg.hasFlag(TcpSegment::ACK))
        {
            return out;
        }

        out.push_back(TcpOutgoing{remoteIp, makeReset(seg)});
        std::cout << "TCP segment for port " << seg.destinationPort()
                  << " has no connection; RST sent.\n";
        return out;
    }

    Connection &connection = it->second;

    // ---- SYN-RECEIVED ----
    if (connection.state == TcpState::SynReceived)
    {
        if (seg.hasFlag(TcpSegment::RST))
        {
            if (seg.sequenceNumber() == connection.rcvNxt)
            {
                std::cout << "TCP connection reset by peer during handshake.\n";
                connections_.erase(it);
            }
            return out;
        }

        if (seg.hasFlag(TcpSegment::SYN))
        {
            if (seg.sequenceNumber() == connection.irs) // retransmitted SYN
            {
                out.push_back(TcpOutgoing{
                    remoteIp,
                    makeSynAck(seg.destinationPort(), seg.sourcePort(), connection)});
            }
            return out;
        }

        if (!seg.hasFlag(TcpSegment::ACK))
        {
            return out;
        }

        if (seg.acknowledgmentNumber() != connection.sndNxt)
        {
            out.push_back(TcpOutgoing{remoteIp, makeReset(seg)});
            return out;
        }

        connection.sndUna = seg.acknowledgmentNumber();
        connection.state = TcpState::Established;
        std::cout << "TCP connection established on port " << seg.destinationPort() << ".\n";
        // fall through: the ACK may also carry data or a FIN
    }

    // ---- ESTABLISHED ----
    if (seg.hasFlag(TcpSegment::RST))
    {
        // Only an exact match resets (RFC 5961); in-window-but-inexact RSTs are ignored for now.
        if (seg.sequenceNumber() == connection.rcvNxt)
        {
            std::cout << "TCP connection reset by peer.\n";
            connections_.erase(it);
        }
        return out;
    }

    if (seg.hasFlag(TcpSegment::SYN))
    {
        // Challenge ACK (RFC 5961): a SYN on a synchronized connection is never accepted.
        out.push_back(TcpOutgoing{
            remoteIp,
            TcpSegment(seg.destinationPort(), seg.sourcePort(),
                       connection.sndNxt, connection.rcvNxt,
                       TcpSegment::ACK, RECEIVE_WINDOW)});
        return out;
    }

    if (seg.hasFlag(TcpSegment::ACK) &&
        seqLt(connection.sndUna, seg.acknowledgmentNumber()) &&
        seqLeq(seg.acknowledgmentNumber(), connection.sndNxt))
    {
        connection.sndUna = seg.acknowledgmentNumber();
    }

    if (seg.sequenceLength() > 0)
    {
        std::cout << "TCP data/FIN received but not handled yet.\n";
    }

    return out;
}