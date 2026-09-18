#pragma once
#include <array>
#include <cstdint>
#include <vector>

using MacAddress = std::array<std::uint8_t, 6>;

class ArpPacket
{

public:
    static ArpPacket parse(const std::vector<unsigned char> &data);

    std::uint16_t hardwareType() const;
    std::uint16_t protocolType() const;
    std::uint16_t opCode() const;
    std::uint16_t hardwareSize() const;
    std::uint16_t protocolSize() const;

    MacAddress senderMac() const;
    MacAddress targetMac() const;

    std::uint32_t senderIp() const;
    std::uint32_t targetIp() const;

private:
    std::uint16_t hardware_type_ = 0;
    std::uint16_t protocol_type_ = 0;
    std::uint16_t op_code_ = 0;
    std::uint16_t hardware_size_ = 0;
    std::uint16_t protocol_size_ = 0;

    MacAddress sender_mac_{};
    MacAddress target_mac_{};

    std::uint32_t sender_ip_ = 0;
    std::uint32_t target_ip_ = 0;
};
