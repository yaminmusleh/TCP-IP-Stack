#include "net/arp_cache.hpp"

void ArpCache::add(std::uint32_t ip, const MacAddress &mac)
{
    entries_[ip] = mac;
    // entries_[ip] = mac; inserts the mapping or updates it if the IP already exists.
}

std::optional<MacAddress> ArpCache::lookup(std::uint32_t ip) const
{
    auto it = entries_.find(ip); // find(ip) searches without modifying the cache.

    // we will iterate the map entries_

    if (it == entries_.end()) // .end(): if it reached the end, the IP isn't present in the cache.
    {
        return std::nullopt;
    }
    // If the IP isn't present, std::nullopt means “not found.”

    return it->second; // If found, return the corresponding MAC address.
}