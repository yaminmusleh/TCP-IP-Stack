#include <iostream>
#include "net/tap.hpp"
#include "net/ethernet.hpp"
#include "net/arp.hpp"
#include "net/ipv4.hpp"
#include "net/icmpv4.hpp"

int main()
{
    TapDevice tap("tap0");

    std::uint32_t ourIp =
        (10u << 24) |
        (0u << 16) |
        (0u << 8) |
        1u;

    while (true)
    {
        std::cout << "Waiting for an Ethernet frame...\n";

        auto raw_frame = tap.readFrame();

        EthernetFrame ethernet =
            EthernetFrame::parse(raw_frame);

        if (ethernet.etherType() == 0x0800)
        {
            std::cout << "Received an IPv4 frame\n";

            Ipv4Packet ipv4 =
                Ipv4Packet::parse(ethernet.payload());

            std::cout << "IPv4 packet received.\n";

            std::cout << "Source IP: "
                      << ipv4.sourceIp()
                      << "\n";

            std::cout << "Destination IP: "
                      << ipv4.destinationIp()
                      << "\n";

            std::cout << "Protocol: "
                      << static_cast<int>(ipv4.protocol())
                      << "\n";

            if (ipv4.protocol() == 1)
            {
                Icmpv4Packet icmp =
                    Icmpv4Packet::parse(ipv4.payload());

                std::cout << "ICMP packet received.\n";

                std::cout << "ICMP type: "
                          << static_cast<int>(icmp.type())
                          << "\n";

                std::cout << "ICMP code: "
                          << static_cast<int>(icmp.code())
                          << "\n";

                if (icmp.type() == 8 && icmp.code() == 0)
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
                }
            }

            continue;
        }

        if (ethernet.etherType() != 0x0806)
        {
            std::cout << "Unknown EtherType: 0x"
                      << std::hex
                      << ethernet.etherType()
                      << std::dec
                      << "\n";

            continue;
        }

        ArpPacket arp =
            ArpPacket::parse(ethernet.payload());

        if (arp.opCode() != 1)
        {
            std::cout << "Not an ARP request. Ignoring.\n";
            continue;
        }

        if (arp.targetIp() != ourIp)
        {
            std::cout << "ARP request is not for us. Ignoring.\n";
            continue;
        }

        // ARP REPLY CONSTRUCTION
        MacAddress ourMac = {
            0x56, 0x1d, 0x27, 0x4d, 0xbf, 0x83};

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
            0x0806,
            replyPayLoad);

        tap.writeFrame(replyFrame);

        std::cout << "ARP Reply Sent!\n";
    }
}