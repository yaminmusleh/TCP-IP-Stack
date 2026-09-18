#pragma once

#include <string>
#include <cstddef>
#include <vector>

class TapDevice
{

    // i need a descriptor with value -1 which means there is no file descriptor at the moment
private:
    int fd_ = -1;
    std::string name_;

public:
    explicit TapDevice(const std::string &name); // explicit prevents implicit conversion to TapDevice
    ~TapDevice();

    std::vector<unsigned char> readFrame();
    void writeFrame(const std::vector<unsigned char> &frame);
};