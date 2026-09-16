#include <iostream>
#include <iomanip>

#include "net/tap.hpp"
#include "net/ethernet.hpp"
#include "net/arp.hpp"

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

        std::cout << "ARP hardware type: "
                  << arp.hardwareType()
                  << '\n';

        std::cout << "ARP protocol type: 0x"
                  << std::hex
                  << arp.protocolType()
                  << std::dec
                  << '\n';

        std::cout << "ARP opcode: "
                  << arp.opCode()
                  << '\n';
    }
}