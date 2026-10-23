#include "net/tcp.hpp"
#include "net/checksum.hpp"

#include <stdexcept>

namespace
{
    constexpr std::uint8_t IP_PROTOCOL_TCP = 6;

    std::vector<unsigned char> buildTcpPseudoHeader(std::uint32_t sourceIp,
                                                    std::uint32_t destinationIp,
                                                    std::uint16_t tcpLength)
    {
        std::vector<unsigned char> p;
        p.reserve(12);

        p.push_back(static_cast<unsigned char>(sourceIp >> 24));
        p.push_back(static_cast<unsigned char>(sourceIp >> 16));
        p.push_back(static_cast<unsigned char>(sourceIp >> 8));
        p.push_back(static_cast<unsigned char>(sourceIp));

        p.push_back(static_cast<unsigned char>(destinationIp >> 24));
        p.push_back(static_cast<unsigned char>(destinationIp >> 16));
        p.push_back(static_cast<unsigned char>(destinationIp >> 8));
        p.push_back(static_cast<unsigned char>(destinationIp));

        p.push_back(0);
        p.push_back(IP_PROTOCOL_TCP);

        p.push_back(static_cast<unsigned char>(tcpLength >> 8));
        p.push_back(static_cast<unsigned char>(tcpLength));

        return p;
    }
}