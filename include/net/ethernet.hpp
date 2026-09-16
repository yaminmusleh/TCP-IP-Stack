#pragma once

#include <cstdint>
#include <vector>

class EthernetFrame
{

public:                                                                 // building the constructor and getters
    static EthernetFrame parse(const std::vector<unsigned char> &data); // we send data here by reference

    const std::vector<unsigned char> &destinationMac() const;
    const std::vector<unsigned char> &sourceMac() const;
    std::uint16_t etherType() const;
    const std::vector<unsigned char> &payload() const;
    // these above are considered getters. we will use them in ethernet.cpp

private: // build the setters
    std::vector<unsigned char> destination_mac_;
    std::vector<unsigned char> source_mac_;
    std::uint16_t ether_type_ = 0; // because its 2 bytes = 16bit and uint16_t represents an int of 16bit
    std::vector<unsigned char> payload_;
};