#pragma once

#include <string>

class TapDevice
{

    // i need a descriptor with value -1 which means there is no file descriptor at the moment
private:
    int fd_ = -1;
    std::string name_;

public:
    explicit TapDevice(const std::string &name); //explicit prevents the constructor from doing a configureDevice
    ~TapDevice();
};