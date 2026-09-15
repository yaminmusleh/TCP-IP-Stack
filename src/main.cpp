#include <iostream>
#include <iomanip>

#include "net/tap.hpp"
#include "net/ethernet.hpp"

int main()
{
    TapDevice tap("tap0");

    std::cout << "Waiting for an Ethernet frame...\n";

    auto frame = tap.readFrame();

    EthernetFrame ethernet = EthernetFrame::parse(frame);

    std::cout << "Received frame: "
              << frame.size()
              << " bytes\n";

    std::cout << "EtherType: 0x"
              << std::hex
              << std::setw(4)
              << std::setfill('0')
              << ethernet.etherType()
              << std::dec
              << '\n';
}