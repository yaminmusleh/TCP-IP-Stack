#include "net/ethernet.hpp"
#include <iostream>
#include <stdexcept>

EthernetFrame EthernetFrame::parse(
    const std::vector<unsigned char> &data)
{

    if (data.size() < 14)
    { // if it was smaller than the standard 14 byte frame header throw an error
        throw std::runtime_error("Ethernet frame is too small");
    }

    EthernetFrame frame;

    frame.destination_mac_ =
        std::vector<unsigned char>(data.begin(), data.begin() + 6); // extract from begining to 6th address (0 -> 5)

    frame.source_mac_ =
        std::vector<unsigned char>(data.begin() + 6, data.begin() + 12); // extract from 6th address to 12th (6 -> 11)

    frame.ether_type_ = (static_cast<std::uint16_t>(data[12]) << 8) | static_cast<std::uint16_t>(data[13]);

    frame.payload_ =
        std::vector<unsigned char>(data.begin() + 14, data.end()); // extract what's after the 14th byte

    return frame;
}

std::vector<unsigned char> EthernetFrame::build(
    const std::vector<unsigned char> &destinationMac,
    const std::vector<unsigned char> &sourceMac,
    std::uint16_t etherType,
    const std::vector<unsigned char> &payload)
{
    if (destinationMac.size() != 6 || sourceMac.size() != 6)
    {
        throw std::runtime_error(
            "MAC address must contain 6 bytes");
    }

    std::vector<unsigned char> frame;

    frame.reserve(14 + payload.size());

    frame.insert(frame.end(), destinationMac.begin(), destinationMac.end());

    frame.insert(frame.end(), sourceMac.begin(), sourceMac.end());

    // we will push at the end of a vector the etherType bytes
    frame.push_back(static_cast<unsigned char>(etherType >> 8));
    frame.push_back(static_cast<unsigned char>(etherType));

    frame.insert(frame.end(), payload.begin(), payload.end());

    return frame;
}
// getters: (return what you stored)

const std::vector<unsigned char> &EthernetFrame::destinationMac() const
{
    return destination_mac_;
}
const std::vector<unsigned char> &EthernetFrame::sourceMac() const
{
    return source_mac_;
}
std::uint16_t EthernetFrame::etherType() const
{
    return ether_type_;
}
const std::vector<unsigned char> &EthernetFrame::payload() const
{
    return payload_;
}