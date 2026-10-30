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

std::optional<std::uint32_t> TcpEngine::congestionWindow(std::uint32_t remoteIp,
                                                         std::uint16_t remotePort,
                                                         std::uint16_t localPort) const
{
    auto it = connections_.find(Key{remoteIp, remotePort, localPort});

    if (it == connections_.end())
    {
        return std::nullopt;
    }

    return it->second.cwnd;
}

std::optional<std::uint32_t> TcpEngine::slowStartThreshold(std::uint32_t remoteIp,
                                                           std::uint16_t remotePort,
                                                           std::uint16_t localPort) const
{
    auto it = connections_.find(Key{remoteIp, remotePort, localPort});

    if (it == connections_.end())
    {
        return std::nullopt;
    }

    return it->second.ssthresh;
}

// Sender maximum segment size: the peer's MSS capped by ours, never below 1.
std::uint32_t TcpEngine::smss(const Connection &connection)
{
    const std::uint32_t peer = connection.peerMss.value_or(DEFAULT_PEER_MSS);

    return std::max<std::uint32_t>(1, std::min<std::uint32_t>(peer, LOCAL_MSS));
}

// The receive window we advertise. Received data is handed to the application immediately, so
// the only thing that can fill up is the echo/send queue; we advertise its free space, and
// advertise 0 instead of a window smaller than one MSS (receiver-side silly window avoidance).
std::uint16_t TcpEngine::advertisedWindow(const Connection &connection)
{
    const std::size_t used = connection.sendQueue.size();
    const std::size_t free = used >= MAX_SEND_QUEUE ? 0 : MAX_SEND_QUEUE - used;
    const std::size_t window = std::min<std::size_t>(free, RECEIVE_WINDOW);

    return window < LOCAL_MSS ? 0 : static_cast<std::uint16_t>(window);
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
                      TcpSegment::ACK, advertisedWindow(connection));
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
        flags = static_cast<std::uint8_t>(flags | TcpSegment::FIN);
    }

    if (!sent.data.empty())
    {
        flags = static_cast<std::uint8_t>(flags | TcpSegment::PSH);
    }

    // The ACK and window fields always carry our latest values, not those of the original send.
    return TcpSegment(localPort, remotePort,
                      sent.seq, connection.rcvNxt,
                      flags, advertisedWindow(connection), sent.data);
}

void TcpEngine::armTimer(Connection &connection)
{
    if (!connection.rtoDeadline)
    {
        connection.rtoDeadline = now_ + connection.rto;
    }
}

void TcpEngine::sampleRtt(Connection &connection, std::uint64_t rttMs)
{
    if (!connection.srtt)
    {
        connection.srtt = rttMs;
        connection.rttvar = rttMs / 2;
    }
    else
    {
        const std::uint64_t srtt = *connection.srtt;
        const std::uint64_t diff = srtt > rttMs ? srtt - rttMs : rttMs - srtt;

        connection.rttvar = (3 * connection.rttvar + diff) / 4;
        connection.srtt = (7 * srtt + rttMs) / 8;
    }

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
    bool ackedRetransmitted = false;
    std::optional<std::uint64_t> newestSentAt; // send time of the newest never-retransmitted segment acked

    while (!connection.unacked.empty())
    {
        SentSegment &sent = connection.unacked.front();

        const std::uint32_t length = static_cast<std::uint32_t>(sent.data.size()) +
                                     (sent.syn ? 1u : 0u) + (sent.fin ? 1u : 0u);
        const std::uint32_t end = sent.seq + length;

        if (seqLeq(end, ack))
        {
            if (sent.retransmitted)
            {
                ackedRetransmitted = true;
            }
            else
            {
                newestSentAt = sent.sentAt;
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

    // RFC 6298 / Karn: at most one RTT sample per ACK, taken from the newest segment it
    // acknowledges. If the ACK covered retransmitted data, take none: segments sent behind a
    // lost one are only acknowledged after the retransmission arrives, so their apparent RTT
    // measures the loss recovery, not the path, and would inflate the RTO.
    if (!ackedRetransmitted && newestSentAt)
    {
        sampleRtt(connection, now_ - *newestSentAt);
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

// New data acknowledged (RFC 5681 3.1 slow start / congestion avoidance, RFC 6582 recovery exit).
void TcpEngine::onNewAckCongestion(Connection &connection, std::uint32_t ack,
                                   std::uint32_t bytesAcked, bool &retransmitFront)
{
    const std::uint32_t mss = smss(connection);

    connection.dupAcks = 0;

    if (connection.inRecovery)
    {
        if (seqGeq(ack, connection.recover))
        {
            // Full ACK: everything outstanding at loss time is acknowledged; deflate the window.
            connection.cwnd = connection.ssthresh;
            connection.inRecovery = false;
        }
        else
        {
            // Partial ACK: the next hole is lost too. Retransmit it, deflate by the amount
            // acknowledged and add back one segment.
            connection.cwnd = connection.cwnd > bytesAcked ? connection.cwnd - bytesAcked : 0;
            connection.cwnd += mss;
            retransmitFront = true;
        }

        return;
    }

    if (connection.cwnd < connection.ssthresh)
    {
        connection.cwnd += std::min(bytesAcked, mss); // slow start: grow by up to one MSS per ACK
    }
    else
    {
        // congestion avoidance: about one MSS per round trip
        connection.cwnd += std::max<std::uint32_t>(1, mss * mss / std::max<std::uint32_t>(connection.cwnd, 1));
    }

    connection.cwnd = std::min<std::uint32_t>(connection.cwnd, 1u << 30); // keep the counter bounded
}

// A duplicate ACK (RFC 5681 3.2): fast retransmit on the third, window inflation afterwards.
void TcpEngine::onDupAck(Connection &connection, std::uint32_t ack, bool &retransmitFront)
{
    const std::uint32_t mss = smss(connection);

    ++connection.dupAcks;

    if (connection.inRecovery)
    {
        connection.cwnd += mss; // each dup ACK means a segment left the network
        return;
    }

    // RFC 6582: only enter fast retransmit if the ACK covers more than the last recovery point.
    if (connection.dupAcks == DUP_ACK_THRESHOLD && seqGt(ack, connection.recover))
    {
        const std::uint32_t flight = connection.sndNxt - connection.sndUna;

        connection.ssthresh = std::max<std::uint32_t>(flight / 2, 2 * mss);
        connection.cwnd = connection.ssthresh + 3 * mss;
        connection.recover = connection.sndNxt;
        connection.inRecovery = true;
        retransmitFront = true;

        std::cout << "TCP fast retransmit (3 duplicate ACKs); ssthresh=" << connection.ssthresh
                  << " cwnd=" << connection.cwnd << ".\n";
    }
}

// Retransmission timeout (RFC 5681 3.1 equation 4): the network is in serious trouble.
void TcpEngine::onTimeoutCongestion(Connection &connection)
{
    const std::uint32_t mss = smss(connection);
    const std::uint32_t flight = connection.sndNxt - connection.sndUna;

    connection.ssthresh = std::max<std::uint32_t>(flight / 2, 2 * mss);
    connection.cwnd = mss;
    connection.dupAcks = 0;
    connection.inRecovery = false;
    connection.recover = connection.sndNxt;
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

    const std::size_t mss = smss(connection);

    bool sent = false;

    while (!connection.sendQueue.empty())
    {
        // Usable window = min(peer window, congestion window) minus what is already in flight.
        const std::uint32_t inFlight = connection.sndNxt - connection.sndUna;
        const std::uint32_t peerSpace = inFlight >= connection.sndWnd ? 0 : connection.sndWnd - inFlight;
        const std::uint32_t cwndSpace = inFlight >= connection.cwnd ? 0 : connection.cwnd - inFlight;
        const std::uint32_t space = std::min(peerSpace, cwndSpace);

        if (space == 0)
        {
            break; // wait for the peer to open its window or for ACKs to grow the congestion window
        }

        const std::size_t n = std::min<std::size_t>(
            {connection.sendQueue.size(), static_cast<std::size_t>(space), mss});

        // Don't dribble out sub-MSS segments just because the congestion window has a few bytes left.
        if (n < mss && n < connection.sendQueue.size() && cwndSpace < peerSpace)
        {
            break;
        }

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
                       flags, advertisedWindow(connection), chunk)});

        connection.unacked.push_back(
            SentSegment{connection.sndNxt, chunk, false, false, now_, false});
        armTimer(connection);

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
                       advertisedWindow(connection))});

        connection.unacked.push_back(
            SentSegment{connection.sndNxt, {}, false, true, now_, false});
        armTimer(connection);

        connection.sndNxt += 1; // FIN consumes one sequence number
        connection.finSent = true;
        connection.state = TcpState::LastAck;
        sent = true;
        std::cout << "TCP FIN sent; waiting for final ACK.\n";
    }

    // Persist timer (RFC 9293 3.8.6.1): if the peer's window is closed and nothing is in flight
    // to provoke an ACK, schedule a zero-window probe so a lost window update can't deadlock us.
    if (!connection.sendQueue.empty() && connection.sndWnd == 0 && connection.unacked.empty())
    {
        if (!connection.persistDeadline)
        {
            connection.persistDeadline = now_ + connection.rto;
        }
    }
    else
    {
        connection.persistDeadline.reset();
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

        const std::uint32_t remoteIp = std::get<0>(it->first);
        const std::uint16_t remotePort = std::get<1>(it->first);
        const std::uint16_t localPort = std::get<2>(it->first);

        // ---- zero-window probe: send one byte of queued data ----
        if (connection.persistDeadline && *connection.persistDeadline <= now_)
        {
            connection.persistDeadline.reset();

            const bool canProbe = (connection.state == TcpState::Established ||
                                   connection.state == TcpState::CloseWait) &&
                                  !connection.sendQueue.empty() && connection.sndWnd == 0 &&
                                  connection.unacked.empty();

            if (canProbe)
            {
                std::vector<unsigned char> probe{connection.sendQueue.front()};
                connection.sendQueue.pop_front();

                out.push_back(TcpOutgoing{
                    remoteIp,
                    TcpSegment(localPort, remotePort,
                               connection.sndNxt, connection.rcvNxt,
                               TcpSegment::ACK, advertisedWindow(connection), probe)});

                connection.unacked.push_back(
                    SentSegment{connection.sndNxt, probe, false, false, now_, false});
                armTimer(connection);

                connection.sndNxt += 1;
                connection.lastAdvertised = advertisedWindow(connection);

                std::cout << "TCP zero-window probe sent.\n";
            }
        }

        // ---- retransmission timer ----
        if (!connection.rtoDeadline || *connection.rtoDeadline > now_ || connection.unacked.empty())
        {
            ++it;
            continue;
        }

        const unsigned limit = connection.state == TcpState::SynReceived
                                   ? MAX_SYN_RETRANSMITS
                                   : MAX_DATA_RETRANSMITS;

        // A peer that keeps answering with a zero window is alive; never abort because of that.
        if (connection.retransmitCount >= limit && connection.sndWnd != 0)
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

        // Timeouts caused by a closed peer window (zero-window probes) are not congestion.
        if (connection.state != TcpState::SynReceived && connection.sndWnd != 0)
        {
            onTimeoutCongestion(connection);
        }

        SentSegment &oldest = connection.unacked.front();
        oldest.retransmitted = true;

        out.push_back(TcpOutgoing{
            remoteIp, makeRetransmission(it->first, connection, oldest)});

        connection.lastAdvertised = advertisedWindow(connection);

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

            // An announced MSS of 0 would make the send loop spin forever; treat it as 1.
            if (connection.peerMss && *connection.peerMss == 0)
            {
                connection.peerMss = 1;
            }

            // Initial congestion window (RFC 6928): min(10*MSS, max(2*MSS, 14600)).
            const std::uint32_t mss = smss(connection);
            connection.cwnd = std::min<std::uint32_t>(10 * mss, std::max<std::uint32_t>(2 * mss, 14600));
            connection.recover = connection.iss;

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
    const std::uint32_t length = seg.sequenceLength();

    if (seqGt(ack, connection.sndNxt))
    {
        // acknowledges something we never sent
        out.push_back(TcpOutgoing{
            remoteIp, makeAck(seg.destinationPort(), seg.sourcePort(), connection)});
        return out;
    }

    // Duplicate ACK (RFC 5681 2): same ACK as sndUna, no data/SYN/FIN, same window, data outstanding.
    // A closed peer window is excluded: those ACKs answer zero-window probes, not lost segments.
    const bool isDupAck = ack == connection.sndUna && length == 0 &&
                          !connection.unacked.empty() && connection.sndWnd != 0 &&
                          seg.window() == connection.sndWnd;

    bool retransmitFront = false;

    if (seqLt(connection.sndUna, ack))
    {
        const std::uint32_t bytesAcked = ack - connection.sndUna;

        connection.sndUna = ack;
        onAck(connection, ack);
        onNewAckCongestion(connection, ack, bytesAcked, retransmitFront);
    }
    else if (isDupAck)
    {
        onDupAck(connection, ack, retransmitFront);
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

    // Fast retransmit / partial-ACK retransmission of the oldest unacknowledged segment.
    if (retransmitFront && !connection.unacked.empty())
    {
        SentSegment &front = connection.unacked.front();
        front.retransmitted = true;

        out.push_back(TcpOutgoing{remoteIp, makeRetransmission(key, connection, front)});
    }

    // ---- Incoming data and FIN ----
    bool ackNeeded = false;

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

            // Accept no more than the window we advertised; the peer retransmits the rest.
            const std::size_t take = std::min<std::size_t>(
                payload.size() - skip, advertisedWindow(connection));

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
    else if (!sent && connection.lastAdvertised == 0 && advertisedWindow(connection) > 0)
    {
        // Our receive window re-opened: tell the peer instead of waiting for its next probe.
        out.push_back(TcpOutgoing{
            remoteIp, makeAck(seg.destinationPort(), seg.sourcePort(), connection)});
    }

    if (!out.empty())
    {
        connection.lastAdvertised = advertisedWindow(connection);
    }

    return out;
}