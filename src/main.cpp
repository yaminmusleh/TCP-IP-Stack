#include <iostream>
#include "net/tap.hpp"

int main()
{
    std::cout << "Starting program...\n";

    TapDevice tap("tap0");

    std::cout << "TAP device created.\n";
    std::cout << "Waiting for an Ethernet frame...\n";

    auto frame = tap.readFrame();

    std::cout << "Received frame: "
          << frame.size()
          << " bytes\n";

    std::cout << "Raw frame:\n";

    for (unsigned char byte : frame)
    {
        std::cout << std::hex
                  << static_cast<int>(byte)
                  << ' ';
    }

    std::cout << std::dec << '\n';
}