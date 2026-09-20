#include <iostream>
#include <iomanip>
#include <array>
#include "net/tap.hpp"
#include "net/ethernet.hpp"
#include "net/arp.hpp"

void printMac(const MacAddress &mac)
{
    for (std::size_t i = 0; i < mac.size(); ++i)
    {
        if (i != 0)
        {
            std::cout << ":";
        }

        std::cout << std::hex
                  << std::setw(2)
                  << std::setfill('0')
                  << static_cast<int>(mac[i]);
    }
    std::cout << std::dec << '\n';
}

void printIp(std::uint32_t ip) // this function prints IP
{
    std::cout
        << ((ip >> 24) & 0xff) << '.'
        << ((ip >> 16) & 0xff) << '.'
        << ((ip >> 8) & 0xff) << '.'
        << (ip & 0xff)
        << '\n';
}

int main()
{
    TapDevice tap("tap0");

    while (true)
    {
        std::cout << "Waiting for an Ethernet frame...\n";

        auto raw_frame = tap.readFrame();

        EthernetFrame ethernet =
            EthernetFrame::parse(raw_frame);

        std::cout << "EtherType: 0x"
                  << std::hex
                  << std::setw(4)
                  << std::setfill('0')
                  << ethernet.etherType()
                  << std::dec
                  << '\n';

        if (ethernet.etherType() != 0x0806) // since we said the ARP Frame is 0x0806, anything else is not an ARP frame.
        {
            std::cout << "Not an ARP frame. Ignoring.\n";
            continue;
        }

        ArpPacket arp =
            ArpPacket::parse(ethernet.payload());

        auto serialized_arp = arp.serialize();

        ArpPacket parsed_again = ArpPacket::parse(serialized_arp);

        if (parsed_again.hardwareType() == arp.hardwareType() &&
            parsed_again.protocolType() == arp.protocolType() &&
            parsed_again.hardwareSize() == arp.hardwareSize() &&
            parsed_again.protocolSize() == arp.protocolSize() &&
            parsed_again.opCode() == arp.opCode() &&
            parsed_again.senderMac() == arp.senderMac() &&
            parsed_again.targetMac() == arp.targetMac() &&
            parsed_again.senderIp() == arp.senderIp() &&
            parsed_again.targetIp() == arp.targetIp())
        {
            std::cout << "ARP round-trip successful!\n";
        }
        else
        {
            std::cout << "ARP round-trip failed!\n";
        }

        std::cout << "Serialized ARP size: "
                  << serialized_arp.size()
                  << " bytes\n";

        std::cout << "ARP hardware type: "
                  << arp.hardwareType()
                  << '\n';

        std::cout << "ARP protocol type: 0x"
                  << std::hex
                  << arp.protocolType()
                  << std::dec
                  << '\n';

        std::cout << "ARP hardware size: "
                  << static_cast<int>(arp.hardwareSize())
                  << '\n';

        std::cout << "ARP protocol size: "
                  << static_cast<int>(arp.protocolSize())
                  << '\n';

        std::cout << "ARP opcode: "
                  << arp.opCode()
                  << '\n';

        std::cout << "Sender Mac: ";
        printMac(arp.senderMac());

        std::cout << "Target Mac: ";
        printMac(arp.targetMac());

        std::cout << "Sender IP: ";
        printIp(arp.senderIp());

        std::cout << "Target IP: ";
        printIp(arp.targetIp());

        // ARP REPLY CONSTRUCTION
        MacAddress ourMac = {
            0x56, 0x1d, 0x27, 0x4d, 0xbf, 0x83};

        std::uint32_t ourIp =
            (10u << 24) |
            (0u << 16) |
            (0u << 8) |
            1u;

        ArpPacket reply =
            ArpPacket::createReply(arp, ourMac, ourIp);

        auto replyPayLoad = reply.serialize();

        std::cout << "Reply Payload Size: "
                  << replyPayLoad.size()
                  << " bytes\n";

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

        std::cout << "Complete reply frame size: "
                  << replyFrame.size()
                  << " bytes\n";
    }
}