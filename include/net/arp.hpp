#pragma once

#include <cstdint>
#include <vector>

class ArpPacket
{
public:
    static ArpPacket parse(const std::vector<unsigned char> &data);

    std::uint16_t hardwareType() const;
    std::uint16_t protocolType() const;
    std::uint16_t opCode() const;

private:
    std::uint16_t hardware_type_ = 0;
    std::uint16_t protocol_type_ = 0;
    std::uint16_t op_code_ = 0;
};
