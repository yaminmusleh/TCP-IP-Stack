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
TapDevice::~TapDevice()
{
    if (fd_ > 0)
    {
        close(fd_);
    }
}