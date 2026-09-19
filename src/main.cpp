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
    }
}