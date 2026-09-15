#include <iostream>
#include "net/tap.hpp"
int main()
{
    TapDevice tap("tap0");
    
    std::cout << "TAP device created. Press Enter to exit...\n";
    std::cin.get();
}