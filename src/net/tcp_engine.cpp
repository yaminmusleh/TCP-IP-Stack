#include "net/tcp_engine.hpp"

#include <algorithm>
#include <iostream>
#include <utility>

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

void TcpEngine::setDataHandler(DataHandler handler)
{
    dataHandler_ = std::move(handler);
}

void TcpEngine::advanceTime(std::uint64_t nowMs)
{
    if (nowMs > now_)
    {
        now_ = nowMs;
    }
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

TcpSegment TcpEngine::makeSynAck(std::uint16_t localPort, std::uint16_t remotePort,
                                 const Connection &connection)
{
    TcpSegment synAck(localPort, remotePort,
                      connection.iss, connection.rcvNxt,
                      static_cast<std::uint8_t>(TcpSegment::SYN | TcpSegment::ACK),
                      RECEIVE_WINDOW);

    synAck.setMss(LOCAL_MSS);

    return synAck;
}

TcpSegment TcpEngine::makeAck(std::uint16_t localPort, std::uint16_t remotePort,
                              const Connection &connection)
{
    return TcpSegment(localPort, remotePort,
                      connection.sndNxt, connection.rcvNxt,
                      TcpSegment::ACK, RECEIVE_WINDOW);
}

TcpSegment TcpEngine::makeRetransmission(const Key &key, const Connection &connection,
                                         const SentSegment &sent)
{
    const std::uint16_t remotePort = std::get<1>(key);
    const std::uint16_t localPort = std::get<2>(key);

    if (sent.syn)
    {
        return makeSynAck(localPort, remotePort, connection);
    }

    std::uint8_t flags = TcpSegment::ACK;

    if (sent.fin)
    {
        flags = static_cast<std::uint8_t>(flags | TcpSegment::ACK);
    }

    if (!sent.data.empty())
    {
        flags = static_cast<std::uint8_t>(flags | TcpSegment::PSH);
    }

    return TcpSegment(localPort, remotePort,
                      sent.seq, connection.rcvNxt,
                      flags, RECEIVE_WINDOW, sent.data);
}

void TcpEngine::armTimer(Connection &connection)
{
    if (!connection.rtoDeadline)
    {
        connection.rtoDeadline = now_ + connection.rto;
    }
}

void TcpEngine::sampleRtt(Connection &connection, std::uint64_t rttMs) // rttMs is the sample rtt: Sample RTT is a single, volatile data point
{
    if (!connection.srtt) // initialization: when the very first Round-Trip Time (RTT) measurement is taken at the start of a TCP connection
    {
        connection.srtt = rttMs; // The first Sample RTT becomes the baseline SRTT.

        connection.rttvar = rttMs / 2; // is initialized to exactly half of that value
    }
    else
    {
        const std::uint64_t srtt = *connection.srtt;
        const std::uint64_t diff = srtt > rttMs ? srtt - rttMs : rttMs - srtt;

        // we need smoothing formulas:
        // connection.rttvar = ((3 * rttvar) + diff) / 4
        // connection.srtt = ((7 * srtt) + sampleRTT) / 8 --> the sampleRTT is rttMs here

        connection.rttvar = (3 * connection.rttvar + diff) / 4;
        connection.srtt = (7 * srtt + rttMs) / 8;
    }

    // now calculating RTO:
    // RTO = SRTT + max(G, 4 * RTTVAR), with a 1 ms clock granularity.
    const std::uint64_t rto = *connection.srtt + std::max<std::uint64_t>(4 * connection.rttvar, 1);

    connection.rto = std::clamp<std::uint64_t>(rto, MIN_RTO_MS, MAX_RTO_MS);
}

// Called when the peer's ACK advances sndUna: drops acknowledged records, trims a partially
// acknowledged one, takes an RTT sample (never from retransmitted data, per Karn), and
// restarts or stops the retransmission timer.
void TcpEngine::onAck(Connection &connection, std::uint32_t ack)
{
    bool progress = false;

    while (!connection.unacked.empty())
    {
        SentSegment &sent = connection.unacked.front();

        const std::uint32_t length = static_cast<std::uint32_t>(sent.data.size()) + (sent.syn ? 1u : 0u) + (sent.fin ? 1u : 0u);
        const std::uint32_t end = sent.seq + length;

        if (seqLeq(end, ack))
        {
            if (!sent.retransmitted)
            {
                sampleRtt(connection, now_ - sent.sentAt);
            }

            connection.unacked.pop_front();
            progress = true;
        }
        else
        {
            if (seqLt(sent.seq, ack))
            {
                // Partial ACK of a data chunk: keep only the unacknowledged tail.
                const std::size_t n = std::min<std::size_t>(ack - sent.seq, sent.data.size());

                sent.data.erase(sent.data.begin(),
                                sent.data.begin() + static_cast<std::ptrdiff_t>(n));
                sent.seq += static_cast<std::uint32_t>(n);
                sent.retransmitted = true; // its send time no longer describes this data
                progress = true;
            }
            break;
        }
    }

    if (progress)
    {
        connection.retransmitCount = 0;

        if (connection.unacked.empty())
        {
            connection.rtoDeadline.reset();
        }
        else
        {
            connection.rtoDeadline = now_ + connection.rto;
        }
    }
}

bool TcpEngine::flush(const Key &key, Connection &connection, std::vector<TcpOutgoing> &out)
{
    if (connection.state != TcpState::Established && connection.state != TcpState::CloseWait)
    {
        return false;
    }

    const std::uint32_t remoteIp = std::get<0>(key);
    const std::uint16_t remotePort = std::get<1>(key);
    const std::uint16_t localPort = std::get<2>(key);

    const std::size_t mss = std::min<std::size_t>(
        connection.peerMss.value_or(DEFAULT_PEER_MSS), LOCAL_MSS);

    bool sent = false;

    while (!connection.sendQueue.empty())
    {
        const std::uint32_t inFlight = connection.sndNxt - connection.sndUna;
        const std::uint32_t space =
            inFlight >= connection.sndWnd ? 0 : connection.sndWnd - inFlight;

        if (space == 0)
        {
            break; // wait for the peer to open its window
        }

        const std::size_t n = std::min<std::size_t>(
            {connection.sendQueue.size(), static_cast<std::size_t>(space), mss});

        std::vector<unsigned char> chunk(
            connection.sendQueue.begin(),
            connection.sendQueue.begin() + static_cast<std::ptrdiff_t>(n));

        connection.sendQueue.erase(
            connection.sendQueue.begin(),
            connection.sendQueue.begin() + static_cast<std::ptrdiff_t>(n));

        std::uint8_t flags = TcpSegment::ACK;
        if (connection.sendQueue.empty())
        {
            flags = static_cast<std::uint8_t>(flags | TcpSegment::PSH);
        }

        out.push_back(TcpOutgoing{
            remoteIp,
            TcpSegment(localPort, remotePort,
                       connection.sndNxt, connection.rcvNxt,
                       flags, RECEIVE_WINDOW, chunk)});

        connection.sndNxt += static_cast<std::uint32_t>(n);
        sent = true;
    }

    if (connection.finPending && !connection.finSent && connection.sendQueue.empty())
    {
        out.push_back(TcpOutgoing{
            remoteIp,
            TcpSegment(localPort, remotePort,
                       connection.sndNxt, connection.rcvNxt,
                       static_cast<std::uint8_t>(TcpSegment::FIN | TcpSegment::ACK),
                       RECEIVE_WINDOW)});

        connection.sndNxt += 1; // FIN consumes one sequence number
        connection.finSent = true;
        connection.state = TcpState::LastAck;
        sent = true;
        std::cout << "TCP FIN sent; waiting for final ACK.\n";
    }

    return sent;
}

std::vector<TcpOutgoing> TcpEngine::tick(std::uint64_t nowMs)
{
    advanceTime(nowMs);

    std::vector<TcpOutgoing> out;

    for (auto it = connections_.begin(); it != connections_.end();)
    {
        Connection &connection = it->second;

        if (!connection.rtoDeadline || *connection.rtoDeadline > now_ || connection.unacked.empty())
        {
            ++it;
            continue;
        }

        const std::uint32_t remoteIp = std::get<0>(it->first);
        const std::uint16_t remotePort = std::get<1>(it->first);
        const std::uint16_t localPort = std::get<2>(it->first);

        const unsigned limit = connection.state == TcpState::SynReceived
                                   ? MAX_SYN_RETRANSMITS
                                   : MAX_DATA_RETRANSMITS;

        if (connection.retransmitCount >= limit)
        {
            std::cout << "TCP retransmission limit reached; aborting connection.\n";

            out.push_back(TcpOutgoing{
                remoteIp,
                TcpSegment(localPort, remotePort,
                           connection.sndNxt, connection.rcvNxt,
                           static_cast<std::uint8_t>(TcpSegment::RST | TcpSegment::ACK), 0)});

            it = connections_.erase(it);
            continue;
        }

        SentSegment &oldest = connection.unacked.front();
        oldest.retransmitted = true;

        out.push_back(TcpOutgoing{
            remoteIp, makeRetransmission(it->first, connection, oldest)});

        ++connection.retransmitCount;
        connection.rto = std::min<std::uint64_t>(connection.rto * 2, MAX_RTO_MS);
        connection.rtoDeadline = now_ + connection.rto;

        std::cout << "TCP retransmission #" << connection.retransmitCount
                  << " (next timeout in " << connection.rto << " ms).\n";

        ++it;
    }

    return out;
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
            connection.sndWnd = seg.window();
            connection.sndWl1 = seg.sequenceNumber();
            connection.sndWl2 = connection.iss;
            connection.peerMss = seg.mss();

            connection.unacked.push_back(
                SentSegment{connection.iss, {}, true, false, now_, false});
            armTimer(connection);

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
                for (auto &sent : connection.unacked)
                {
                    sent.retransmitted = true; // no RTT sample from a resent SYN+ACK
                }

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
        onAck(connection, seg.acknowledgmentNumber());
        connection.state = TcpState::Established;
        std::cout << "TCP connection established on port " << seg.destinationPort() << ".\n";
        // fall through: the ACK may also carry data or a FIN
    }

    // ---- ESTABLISHED, CLOSE-WAIT, LAST-ACK ----
    if (seg.hasFlag(TcpSegment::RST))
    {
        // Only an exact match resets (RFC 5961).
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
            remoteIp, makeAck(seg.destinationPort(), seg.sourcePort(), connection)});
        return out;
    }

    if (!seg.hasFlag(TcpSegment::ACK))
    {
        return out; // synchronized states require ACK
    }

    const std::uint32_t ack = seg.acknowledgmentNumber();

    if (seqGt(ack, connection.sndNxt))
    {
        // acknowledges something we never sent
        out.push_back(TcpOutgoing{
            remoteIp, makeAck(seg.destinationPort(), seg.sourcePort(), connection)});
        return out;
    }

    if (seqLt(connection.sndUna, ack))
    {
        connection.sndUna = ack;
        onAck(connection, ack);
    }

    // Window update (RFC 9293 3.10.7.4, ESTABLISHED processing)
    if (seqLeq(connection.sndUna, ack) &&
        (seqLt(connection.sndWl1, seg.sequenceNumber()) ||
         (connection.sndWl1 == seg.sequenceNumber() && seqLeq(connection.sndWl2, ack))))
    {
        connection.sndWnd = seg.window();
        connection.sndWl1 = seg.sequenceNumber();
        connection.sndWl2 = ack;
    }

    if (connection.state == TcpState::LastAck)
    {
        if (connection.finSent && connection.sndUna == connection.sndNxt)
        {
            std::cout << "TCP connection closed.\n";
            connections_.erase(it);
            return out;
        }
    }

    // ---- Incoming data and FIN ----
    bool ackNeeded = false;
    const std::uint32_t length = seg.sequenceLength();

    if (length > 0)
    {
        ackNeeded = true; // anything consuming sequence space is always acknowledged

        if (seqLeq(seg.sequenceNumber() + length, connection.rcvNxt))
        {
            std::cout << "TCP duplicate segment; re-ACKing.\n";
        }
        else if (seqGt(seg.sequenceNumber(), connection.rcvNxt))
        {
            std::cout << "TCP out-of-order segment dropped; duplicate ACK sent.\n";
        }
        else if (connection.state == TcpState::Established)
        {
            const auto &payload = seg.payload();

            // Skip bytes we already received (retransmission overlapping new data).
            const std::size_t skip = std::min<std::size_t>(
                connection.rcvNxt - seg.sequenceNumber(), payload.size());

            std::size_t take = std::min<std::size_t>(payload.size() - skip, RECEIVE_WINDOW);

            if (connection.sendQueue.size() >= MAX_SEND_QUEUE)
            {
                take = 0; // apply backpressure: peer will retransmit later
            }

            if (take > 0)
            {
                std::vector<unsigned char> data(
                    payload.begin() + static_cast<std::ptrdiff_t>(skip),
                    payload.begin() + static_cast<std::ptrdiff_t>(skip + take));

                connection.rcvNxt += static_cast<std::uint32_t>(take);

                if (dataHandler_)
                {
                    auto response = dataHandler_(seg.destinationPort(), data);
                    connection.sendQueue.insert(connection.sendQueue.end(),
                                                response.begin(), response.end());
                }
            }

            if (seg.hasFlag(TcpSegment::FIN) &&
                seg.sequenceNumber() + static_cast<std::uint32_t>(payload.size()) == connection.rcvNxt)
            {
                connection.rcvNxt += 1; // FIN consumes one sequence number
                connection.state = TcpState::CloseWait;
                connection.finPending = true;
                std::cout << "TCP FIN received; connection in CLOSE-WAIT.\n";
            }
        }
    }

    // Data (and our FIN, if due) carry the ACK; otherwise send a bare ACK when needed.
    const bool sent = flush(key, connection, out);

    if (ackNeeded && !sent)
    {
        out.push_back(TcpOutgoing{
            remoteIp, makeAck(seg.destinationPort(), seg.sourcePort(), connection)});
    }

    return out;
}