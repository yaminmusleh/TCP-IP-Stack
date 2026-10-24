#include <iostream>
#include "net/tap.hpp"
#include "net/ethernet.hpp"
#include "net/arp.hpp"
#include "net/ipv4.hpp"
#include "net/icmpv4.hpp"
#include "net/udp.hpp"
#include "net/arp_cache.hpp"
#include "net/tcp.hpp"
#include <optional>
#include <unordered_set>
#include <vector>
#include <exception>
#include <string>
#include <cstdint>
#include <chrono>
#include <iterator>
#include <unordered_map>

constexpr std::uint16_t ETHERTYPE_IPV4 = 0X0800;
constexpr std::uint16_t ETHERTYPE_ARP = 0X0806;
constexpr std::uint16_t ETHERTYPE_IPV6 = 0X86DD;

constexpr std::uint8_t IP_PROTOCOL_ICMP = 1;

constexpr std::uint8_t ICMP_ECHO_REQUEST = 8;

constexpr std::uint8_t IP_PROTOCOL_UDP = 17;

constexpr std::uint8_t IP_PROTOCOL_TCP = 6;

struct PendingPacket
{
    std::uint32_t targetIp;
    std::vector<unsigned char> ipv4Payload;
};

void ArpHandler(const EthernetFrame &ethernet,
                TapDevice &tap,
                std::uint32_t ourIp,
                const MacAddress &ourMac,
                ArpCache &arpCache,
                std::unordered_set<std::uint32_t> &pendingArp)
{
    ArpPacket arp =
        ArpPacket::parse(ethernet.payload());

    if (arp.senderIp() != 0)
    {
        arpCache.add(arp.senderIp(), arp.senderMac());
        // we add them because we want to cache the sender's IP and MAC address for future use.
    }

    if (arp.opCode() == 2)
    {
        pendingArp.erase(arp.senderIp());
        std::cout << "ARP Reply Received; sender mapping cached.\n";
        return;
    }

    if (arp.opCode() != 1)
    {
        std::cout << "Unknown ARP operation. Ignoring.\n";
        return;
    }

    if (arp.targetIp() != ourIp)
    {
        std::cout << "ARP request is not for us. Ignoring.\n";
        return;
    }

    ArpPacket reply =
        ArpPacket::createReply(arp, ourMac, ourIp);

    auto replyPayLoad = reply.serialize();

    // MacAddress is std::array so i need to convert it into vector
    std::vector<unsigned char> destinationMac = {
        reply.targetMac()[0],
        reply.targetMac()[1],
        reply.targetMac()[2],
        reply.targetMac()[3],
        reply.targetMac()[4],
        reply.targetMac()[5]};

    std::vector<unsigned char> sourceMac = {
        reply.senderMac()[0],
        reply.senderMac()[1],
        reply.senderMac()[2],
        reply.senderMac()[3],
        reply.senderMac()[4],
        reply.senderMac()[5]};

    auto replyFrame = EthernetFrame::build(
        destinationMac,
        sourceMac,
        ETHERTYPE_ARP,
        replyPayLoad);

    tap.writeFrame(replyFrame);

    std::cout << "ARP Reply Sent!\n";
}

void SendArpRequest(TapDevice &tap, MacAddress ourMac,
                    std::uint32_t ourIp, std::uint32_t targetIp)
{
    MacAddress broadcastMac = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    ArpPacket arpRequest =
        ArpPacket::createRequest(ourMac, ourIp, targetIp);

    auto arpPayload = arpRequest.serialize();

    std::vector<unsigned char> destinationMac(broadcastMac.begin(), broadcastMac.end());

    std::vector<unsigned char> sourceMac(ourMac.begin(), ourMac.end());

    auto frame = EthernetFrame::build(
        destinationMac,
        sourceMac,
        ETHERTYPE_ARP,
        arpPayload);

    tap.writeFrame(frame);

    std::cout << "ARP Request Sent!\n";
}

std::optional<MacAddress> ResolveMAC(ArpCache &arpCache,
                                     TapDevice &tap,
                                     const MacAddress &ourMac,
                                     std::uint32_t ourIp,
                                     std::uint32_t targetIp,
                                     std::unordered_set<std::uint32_t> &pendingRequests)
{
    auto cached_Mac = arpCache.lookup(targetIp);

    if (cached_Mac.has_value())
    {
        return cached_Mac;
    }

    if (pendingRequests.insert(targetIp).second) // Send only if newly inserted
    {
        SendArpRequest(tap, ourMac, ourIp, targetIp);
    }

    return std::nullopt;

    // Notes:
    // 1. it does Cache Hit: If the MAC address is found in the cache, it returns the cached value.
    // 2. Cache Miss: If the MAC address is not found in the cache, it sends an ARP request to resolve the MAC address and adds the target IP to the pendingRequests set
    // 3. Pending Requests: The pendingRequests set is used to keep track of IP addresses for which ARP requests have been sent but not yet resolved. This prevents sending multiple ARP requests for the same IP address.
}

void SendIpv4Packet(TapDevice &tap,
                    const MacAddress &ourMac,
                    const MacAddress &destinationMac,
                    const std::vector<unsigned char> &ipv4Payload)
{
    std::vector<unsigned char> destination(destinationMac.begin(), destinationMac.end());
    std::vector<unsigned char> source(ourMac.begin(), ourMac.end());

    auto frame = EthernetFrame::build(
        destination,
        source,
        ETHERTYPE_IPV4,
        ipv4Payload);

    tap.writeFrame(frame);
}

void SendIpv4PacketWithARP(TapDevice &tap,
                           ArpCache &arpCache,
                           const MacAddress &ourMac,
                           std::uint32_t ourIp,
                           std::uint32_t targetIp,
                           const std::vector<unsigned char> &ipv4Payload,
                           std::unordered_set<std::uint32_t> &pendingRequests,
                           std::vector<PendingPacket> &pendingPackets)
{
    auto destinationMac = ResolveMAC(arpCache, tap, ourMac, ourIp, targetIp, pendingRequests);

    if (destinationMac.has_value())
    {
        SendIpv4Packet(tap, ourMac, *destinationMac, ipv4Payload);
    }
    else
    {
        pendingPackets.push_back({targetIp, ipv4Payload});
        std::cout << "IPv4 packet queued; waiting for ARP reply.\n";
    }
}

void SendUdpPacket(
    TapDevice &tap,
    ArpCache &arpCache,
    const MacAddress &ourMac,
    std::uint32_t ourIp,
    std::uint32_t targetIp,
    std::uint16_t sourcePort,
    std::uint16_t destinationPort,
    const std::vector<unsigned char> &payload,
    std::unordered_set<std::uint32_t> &pendingRequests,
    std::vector<PendingPacket> &pendingPackets)
{
    // this function basically creates a UDP packet,
    // calculates its checksum, wraps it in an IPv4 packet,
    // and then sends it over the network. If the MAC address for the target IP is not known,
    // it queues the packet for later sending after resolving the MAC address via ARP.

    // we create the UDP packet
    UdpPacket udp(sourcePort, destinationPort, payload);

    // calculate the checksum for the UDP packet using both the source and destination IP addresses
    udp.setChecksum(ourIp, targetIp);

    // serialize the UDP packet to get the raw bytes
    auto udpData = udp.serialize();

    // wrap the UDP packet in an IPv4 packet
    Ipv4Packet ipv4 = Ipv4Packet::create(ourIp, targetIp, IP_PROTOCOL_UDP, udpData);

    // serialize the complete IPv4 packet to get the raw bytes
    auto ipv4Data = ipv4.serialize();

    // send immediately if the MAC address is resolved, otherwise queue it for later sending
    SendIpv4PacketWithARP(tap, arpCache, ourMac, ourIp, targetIp, ipv4Data, pendingRequests, pendingPackets);
}

void FlushPendingPackets(TapDevice &tap,
                         ArpCache &arpCache,
                         const MacAddress &ourMac,
                         std::vector<PendingPacket> &pendingPackets)
{

    auto it = pendingPackets.begin();
    while (it != pendingPackets.end())
    {
        auto destinationMac = arpCache.lookup(it->targetIp);
        if (destinationMac.has_value())
        {
            SendIpv4Packet(tap, ourMac, *destinationMac, it->ipv4Payload);
            it = pendingPackets.erase(it); // Remove the sent packet from the queue
        }
        else
        {
            ++it; // Move to the next packet if MAC address is still not resolved
        }
    }
}

void HandleUdpPayload(const UdpPacket &udp,
                      ArpCache &arpCache,
                      const Ipv4Packet &ipv4,
                      TapDevice &tap,
                      const MacAddress &ourMac,
                      std::uint32_t ourIp,
                      std::unordered_set<std::uint32_t> &pendingArp,
                      std::vector<PendingPacket> &pendingPackets)
{
    // this application handles messages addressed to port 9000
    if (udp.destinationPort() != 9000)
    {
        std::cout << "No application handler for UDP port "
                  << udp.destinationPort() << ".\n";
        return;
    }

    std::cout << "UDP message received on port 9000.\n";
    std::cout << "Message: ";

    const auto &payload = udp.payload();

    std::cout.write(
        reinterpret_cast<const char *>(payload.data()),
        static_cast<std::streamsize>(payload.size()));

    std::cout << "\n";

    const std::string response = "UDP message received";
    std::vector<unsigned char> responsePayload(response.begin(), response.end());

    SendUdpPacket(
        tap,
        arpCache,
        ourMac,
        ourIp,
        ipv4.sourceIp(),
        9000,
        udp.sourcePort(),
        responsePayload,
        pendingArp,
        pendingPackets);
}

void Ipv4Handler(
    EthernetFrame &ethernet,
    TapDevice &tap,
    ArpCache &arpCache,
    const MacAddress &ourMac,
    std::uint32_t ourIp,
    std::unordered_set<std::uint32_t> &pendingArp,
    std::vector<PendingPacket> &pendingPackets)
{
    Ipv4Packet ipv4 = Ipv4Packet::parse(ethernet.payload());

    if (ipv4.destinationIp() != ourIp)
    {
        std::cout << "IPv4 packet is not for us. Ignoring.\n";
        return;
    }

    if ((ipv4.flags() & 0x1) != 0 || ipv4.fragmentOffset() != 0)
    {
        std::cout << "Fragmented IPv4 packet; reassembly not supported. Dropping.\n";
        return;
    }

    std::cout << "IPv4 packet received.\n";

    if (ipv4.protocol() == IP_PROTOCOL_ICMP)
    {
        Icmpv4Packet icmp =
            Icmpv4Packet::parse(ipv4.payload());

        if (icmp.type() == ICMP_ECHO_REQUEST && icmp.code() == 0)
        {
            std::cout << "ICMP Echo Request received (id=" << icmp.identifier()
                      << ", seq=" << icmp.sequence() << ").\n";
            Icmpv4Packet replyIcmp =
                Icmpv4Packet::createEchoReply(icmp);

            auto replyIcmpPayload =
                replyIcmp.serialize();

            Ipv4Packet replyIpv4 =
                Ipv4Packet::createReply(
                    ipv4,
                    replyIcmpPayload);

            auto replyIpv4Payload =
                replyIpv4.serialize();

            std::cout << "ICMP Echo Reply created.\n";

            auto replyFrame = EthernetFrame::build(
                ethernet.sourceMac(), // because we are replying to the source of the request
                ethernet.destinationMac(),
                ETHERTYPE_IPV4,
                replyIpv4Payload);

            tap.writeFrame(replyFrame);

            std::cout << "ICMP Echo Reply sent.\n";
        }
    }

    else if (ipv4.protocol() == IP_PROTOCOL_UDP)
    {
        UdpPacket udp = UdpPacket::parse(ipv4.payload());
        std::cout << "UDP Packet Received.\n";

        if (udp.checksum() == 0)
        {
            std::cout << "UDP checksum omitted (allowed for IPv4).\n";
        }

        else if (!udp.verifyChecksum(ipv4.sourceIp(), ipv4.destinationIp()))
        {
            std::cout << "Invalid UDP Checksum.\n";
            return;
        }

        else
        {
            std::cout << "UDP Checksum Verified.\n";
        }

        std::cout << "Source Port: " << udp.sourcePort() << "\n";
        std::cout << "Destination Port: " << udp.destinationPort() << "\n";
        std::cout << "Payload Size: " << udp.payload().size() << "\n";

        HandleUdpPayload(udp, arpCache, ipv4, tap, ourMac, ourIp, pendingArp, pendingPackets);
    }

    // TCP branch
    else if (ipv4.protocol() == IP_PROTOCOL_TCP)
    {
        TcpSegment tcp = TcpSegment::parse(ipv4.payload());

        if (!tcp.verifyChecksum(ipv4.sourceIp(), ipv4.destinationIp()))
        {
            std::cout << "Invalid TCP checksum. Dropping.\n";
            return;
        }

        std::cout << "TCP segment: " << tcp.sourcePort() << " -> "
                  << tcp.destinationPort()
                  << " seq=" << tcp.sequenceNumber()
                  << " ack=" << tcp.acknowledgmentNumber()
                  << " flags="
                  << (tcp.hasFlag(TcpSegment::SYN) ? "SYN " : "")
                  << (tcp.hasFlag(TcpSegment::ACK) ? "ACK " : "")
                  << (tcp.hasFlag(TcpSegment::FIN) ? "FIN " : "")
                  << (tcp.hasFlag(TcpSegment::RST) ? "RST " : "")
                  << (tcp.hasFlag(TcpSegment::PSH) ? "PSH " : "")
                  << " window=" << tcp.window()
                  << " payload=" << tcp.payload().size() << "\n";
    }
}

constexpr int ARP_MAX_ATTEMPTS = 3;

// RetryPendingArp handles the Address Resolution Protocol (ARP) retry and timeout logic.
// When your network stack wants to send an IPv4 packet to a device on the local network,
// it needs the destination's MAC (Layer 2) address, not just its IP (Layer 3) address.
// If the MAC address isn't already in the local cache, the stack must pause (buffer) the outgoing packets, broadcast an ARP request, and wait for a reply.
// This function runs periodically (e.g., via a timer or event loop tick) to manage those pending lookups.
void RetryPendingArp(TapDevice &tap,
                     const ArpCache &arpCache,
                     const MacAddress &ourMac,
                     std::uint32_t ourIp,
                     std::unordered_set<std::uint32_t> &pendingArp,
                     std::vector<PendingPacket> &pendingPackets,
                     std::unordered_map<std::uint32_t, int> &arpAttempts)
{
    for (auto it = arpAttempts.begin(); it != arpAttempts.end();)
    {
        it = pendingArp.count(it->first) ? std::next(it) : arpAttempts.erase(it);
        // It checks the arpAttempts tracking map.
        // If an IP address is no longer marked as pending (pendingArp), its counter is safely deleted to prevent memory bloat.
    }

    for (auto it = pendingArp.begin(); it != pendingArp.end();)
    {
        const std::uint32_t ip = *it;

        if (arpCache.lookup(ip).has_value())
        {
            it = pendingArp.erase(it);
            continue;
            // Before sending a retry, it checks if the ARP cache now has the MAC address.
        }

        if (++arpAttempts[ip] > ARP_MAX_ATTEMPTS)
        {
            std::cout << "ARP resolution failed; dropping queued packets.\n";
            std::erase_if(pendingPackets,
                          [ip](const PendingPacket &p)
                          { return p.targetIp == ip; });
            arpAttempts.erase(ip);
            it = pendingArp.erase(it);
            continue;
        }

        SendArpRequest(tap, ourMac, ourIp, ip);
        ++it;
    }
}
int main()
{
    TapDevice tap("tap0");

    std::uint32_t ourIp = (10u << 24) | (0u << 16) | (0u << 8) | 1u;

    MacAddress ourMac = {0x56, 0x1d, 0x27, 0x4d, 0xbf, 0x83};

    ArpCache arpCache;

    std::unordered_set<std::uint32_t> pendingArp;

    std::vector<PendingPacket> pendingPackets;

    std::unordered_map<std::uint32_t, int> arpAttempts;
    auto lastArpTick = std::chrono::steady_clock::now();

    // send an initial UDP message to the TAP peer to demonstrate functionality
    const std::uint32_t targetIp = (10u << 24) | (0u << 16) | (0u << 8) | 2u;

    const std::vector<unsigned char> message = {
        'H', 'e', 'l', 'l', 'o', ' ', 'f', 'r', 'o', 'm', ' ',
        'm', 'y', ' ', 'T', 'C', 'P', '/', 'I', 'P', ' ', 's', 't', 'a', 'c', 'k'};

    SendUdpPacket(
        tap,
        arpCache,
        ourMac,
        ourIp,
        targetIp,
        8000, // Source UDP port
        9000, // Destination UDP port
        message,
        pendingArp,
        pendingPackets);

    while (true)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastArpTick >= std::chrono::seconds(1))
        {
            lastArpTick = now;
            try
            {
                RetryPendingArp(tap, arpCache, ourMac, ourIp,
                                pendingArp, pendingPackets, arpAttempts);
            }
            catch (const std::exception &e)
            {
                std::cerr << "ARP retry failed: " << e.what() << '\n';
            }
        }

        if (!tap.waitReadable(250))
        {
            continue;
        }

        auto raw_frame = tap.readFrame();

        try
        {
            EthernetFrame ethernet =
                EthernetFrame::parse(raw_frame);

            // ipv4 handling branch
            if (ethernet.etherType() == ETHERTYPE_IPV4)
            {
                Ipv4Handler(ethernet, tap, arpCache, ourMac, ourIp, pendingArp, pendingPackets);
                continue;
            }
            // arp handling branch
            if (ethernet.etherType() == ETHERTYPE_ARP)
            {
                ArpHandler(ethernet, tap, ourIp, ourMac, arpCache, pendingArp);
                FlushPendingPackets(tap, arpCache, ourMac, pendingPackets);
                continue;
            }
            // ipv6 handling branch
            if (ethernet.etherType() == ETHERTYPE_IPV6)
            {
                // ipv6 is not supported yet, so we will ignore it for now
                continue;
            }
            std::cout << "Unknown EtherType: 0x"
                      << std::hex
                      << ethernet.etherType()
                      << std::dec
                      << "\n";
        }
        catch (const std::exception &e)
        {
            std::cerr << "Dropping malformed packet: "
                      << e.what() << '\n';
        }
    }
}