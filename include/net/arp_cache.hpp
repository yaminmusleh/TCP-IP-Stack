#pragma once

#include "net/arp.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>

class ArpCache
{
public:
    void add(std::uint32_t ip, const MacAddress &mac);

    std::optional<MacAddress> lookup(std::uint32_t ip) const;

private:
    std::unordered_map<std::uint32_t, MacAddress> entries_;
    // the IP will be the key, and the MAC address will be the value
    // entries_ stores IP-to-MAC mappings.
};