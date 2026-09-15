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

    fd_ = open("/dev/net/tun", O_RDWR); //opening tun/tap device of linux since its available on my device

    if (fd_ < 0)
    {
        throw std::runtime_error(
            std::string("Failed to open /dev/net/tun: ") +
            std::strerror(errno)); // errno tells us the error if it returned -1
    }
}
TapDevice::~TapDevice(){
    if(fd_ > 0){
        close(fd_);
    }
}