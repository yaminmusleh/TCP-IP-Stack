#include "net/tap.hpp"

// linux headers:
#include <linux/if_tun.h>
#include <net/if.h>

#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <vector>

TapDevice::TapDevice(const std::string &name)
{

    fd_ = open("/dev/net/tun", O_RDWR); // opening tun/tap device of linux since its available on my device

    if (fd_ < 0)
    {
        throw std::runtime_error(
            std::string("Failed to open /dev/net/tun: ") +
            std::strerror(errno)); // errno tells us the error if it returned -1
    }

    // ------- let’s configure the file descriptor as an actual TAP interface.
    struct ifreq ifr{};
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

    std::strncpy(ifr.ifr_name, name.c_str(), IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    if (ioctl(fd_, TUNSETIFF, &ifr) < 0)
    {

        close(fd_);
        fd_ = -1;

        throw std::runtime_error(
            std::string("Failed to configure TAP device: ") +
            std::strerror(errno));
    }

    name_ = ifr.ifr_name;
}

std::vector<unsigned char> TapDevice::readFrame()
{

    std::vector<unsigned char> buffer(1514);

    ssize_t n = read(fd_, buffer.data(), buffer.size());
    // go into the file, grab as much data as my buffer allows, and tell me exactly how many bytes you actually copied
    // ssize_t returns either a positive, zero or negative number
    if (n < 0)
    {
        throw std::runtime_error(
            std::string("Failed to read from TAP: ") +
            std::strerror(errno));
    }

    buffer.resize(static_cast<std::size_t>(n)); // this means resize the buffer so it mathces the number of bytes stored in n
    // this safely convert n from a signed into an unsigned integer
    // std::size_t converts to unsigned, and because a vector cant be negative we use resize to do this.

    return buffer;
}
TapDevice::~TapDevice()
{
    if (fd_ >= 0) // 0 is valid so thats why we include it.
    {
        close(fd_);
        fd_ = -1;
    }
}