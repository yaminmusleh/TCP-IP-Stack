#include <iostream>
#include "net/tap.hpp"
#include "net/ethernet.hpp"
#include "net/arp.hpp"
#include "net/ipv4.hpp"
#include "net/icmpv4.hpp"
#include "net/udp.hpp"
#include "net/arp_cache.hpp"
#include <optional>
#include <unordered_set>

constexpr std::uint16_t ETHERTYPE_IPV4 = 0X0800;
constexpr std::uint16_t ETHERTYPE_ARP = 0X0806;
constexpr std::uint16_t ETHERTYPE_IPV6 = 0X86DD;

constexpr std::uint8_t IP_PROTOCOL_ICMP = 1;

constexpr std::uint8_t ICMP_ECHO_REQUEST = 8;

constexpr std::uint8_t IP_PROTOCOL_UDP = 17;

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

void Ipv4Handler(EthernetFrame &ethernet, TapDevice &tap)
{
    Ipv4Packet ipv4 =
        Ipv4Packet::parse(ethernet.payload());

    std::cout << "IPv4 packet received.\n";

    if (ipv4.protocol() == IP_PROTOCOL_ICMP)
    {
        Icmpv4Packet icmp =
            Icmpv4Packet::parse(ipv4.payload());

        std::cout << "ICMP Echo Request received.\n";

        if (icmp.type() == ICMP_ECHO_REQUEST && icmp.code() == 0)
        {
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

        if (!udp.verifyChecksum(ipv4.sourceIp(), ipv4.destinationIp()))
        {
            std::cout << "Invalid UDP Checksum.\n";
            return;
        }

        std::cout << "UDP Checksum Verified.\n";

        std::cout << "Source Port: " << udp.sourcePort() << "\n";
        std::cout << "Destination Port: " << udp.destinationPort() << "\n";
        std::cout << "Payload Size: " << udp.payload().size() << "\n";
    }
}
int main()
{
    TapDevice tap("tap0");

    std::uint32_t ourIp =
        (10u << 24) |
        (0u << 16) |
        (0u << 8) |
        1u;

    MacAddress ourMac = {
        0x56, 0x1d, 0x27, 0x4d, 0xbf, 0x83};

    ArpCache arpCache;

    std::unordered_set<std::uint32_t> pendingArp;

    while (true)
    {
        auto raw_frame = tap.readFrame();

        EthernetFrame ethernet =
            EthernetFrame::parse(raw_frame);

        if (ethernet.etherType() == ETHERTYPE_IPV4)
        {
            Ipv4Handler(ethernet, tap);
            continue;
        }
        if (ethernet.etherType() == ETHERTYPE_ARP)
        {
            ArpHandler(ethernet, tap, ourIp, ourMac, arpCache, pendingArp);
            continue;
        }

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
}