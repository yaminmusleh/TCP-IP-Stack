#include "net/tap.hpp"

// linux headers:
#include <linux/if_tun.h>
#include <net/if.h>

#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

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
void TapDevice::writeFrame(const std::vector<unsigned char> &frame)
{
    ssize_t n = write(fd_, frame.data(), frame.size());

    if (n < 0)
    {
        throw std::runtime_error(
            std::string("Failed to write to TAP: ") +
            std::strerror(errno));
    }

    if (static_cast<std::size_t>(n) != frame.size())
    {
        throw std::runtime_error(
            "Incomplete TAP frame write");
    }
}

// TapDevice::waitReadable is a utility function used to non-blocking check or wait for incoming Layer 2 (Ethernet) packets from the operating system's TAP virtual network interface.
bool TapDevice::waitReadable(int timeoutMs)
{
    pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;
    // telling the kernel that the stack is interested in knowing when data is available to read (i.e., an Ethernet frame has arrived from the host kernel).
    int result = poll(&pfd, 1, timeoutMs); // waiting a timeout (poll)

    if (result < 0)
    {
        if (errno == EINTR)
        {
            return false;
        }
        // iff poll() fails (result < 0), it checks if the error was caused by an interrupted system call (EINTR, such as a signal being caught). If so, it gracefully returns false.
        throw std::runtime_error(
            std::string("Failed to poll TAP: ") +
            std::strerror(errno));
        // For any other system error, it throws a descriptive runtime exception.
    }
    return result > 0 && (pfd.revents & POLLIN) != 0; // If result > 0, it means one or more events occurred.
    // It verifies that POLLIN is set in pfd.revents (meaning data is actually ready to be read). If so, it returns true; otherwise, it returns false (which usually indicates a timeout occurred).
}
TapDevice::~TapDevice()
{
    if (fd_ >= 0) // 0 is valid so thats why we include it.
    {
        close(fd_);
        fd_ = -1;
    }
}