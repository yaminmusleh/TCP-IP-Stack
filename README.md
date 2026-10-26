# TCP/IP Stack in C++

A learning-oriented TCP/IP stack implemented in C++20 for Linux, using a TAP virtual network interface to process Ethernet frames in user space.

## Overview

This project explores how computer networking works by implementing core network protocols from scratch rather than relying entirely on the operating system's networking stack for packet processing.

It covers the path from Ethernet frames and ARP address resolution through IPv4, ICMP, UDP, and TCP. The project is designed to make protocol internals understandable through explicit packet parsing, serialization, checksum calculation, connection state, retransmission, and congestion-control logic.

The project is inspired by Saminiir's [Let's Code a TCP/IP Stack](https://www.saminiir.com/lets-code-tcp-ip-stack-1-ethernet-arp/) series.

## Features

### Ethernet and ARP

- Parse and construct Ethernet frames.
- Dispatch received frames according to EtherType.
- Parse ARP requests and replies.
- Generate ARP requests and replies.
- Cache IPv4-to-MAC address mappings.
- Queue outgoing IPv4 packets while waiting for ARP resolution.
- Retry unresolved ARP requests and drop queued packets after the retry limit.

### IPv4 and ICMPv4

- Parse and construct IPv4 packets.
- Validate IPv4 header fields, lengths, and header checksums.
- Dispatch incoming packets according to their IP protocol number.
- Respond to ICMP Echo Requests with Echo Replies, providing basic ping functionality.

### UDP

- Construct, parse, and serialize UDP datagrams.
- Calculate and verify UDP checksums using the IPv4 pseudo-header.
- Validate UDP header and datagram lengths.
- Handle application messages addressed to UDP port `9000`.
- Generate UDP responses.

For IPv4, a UDP checksum of zero is permitted and indicates that no UDP checksum was supplied.

### TCP

TCP is implemented in two main parts: the TCP segment representation and the TCP connection engine.

**TCP segments — `tcp.hpp` and `tcp.cpp`**

- Parse and serialize TCP headers and payloads.
- Handle TCP control flags, including SYN, ACK, FIN, RST, PSH, and URG.
- Calculate and verify TCP checksums.
- Support TCP options, including Maximum Segment Size (MSS).
- Track sequence numbers and acknowledgment numbers.
- Handle sequence-number wraparound using modular comparisons.
- Account for SYN and FIN consuming sequence space.

**TCP engine — `tcp_engine.hpp` and `tcp_engine.cpp`**

- Listen on configured TCP ports.
- Track connections using remote IP address, remote port, and local port.
- Process incoming segments and generate outgoing TCP segments.
- Manage connection establishment and closing.
- Queue application data for transmission and acknowledge received data.
- Track outstanding segments and retransmit data when necessary.
- Manage peer send windows and advertised receive windows.
- Apply congestion-control mechanisms, including slow start, congestion avoidance, fast retransmit, and fast recovery.
- Use retransmission timers and zero-window persist probes.
- Pass received in-order data to an application handler.

The current application configures the TCP engine to listen on port `9000` and echo received bytes back to the sender.

These features demonstrate important TCP mechanisms, but the implementation should not be assumed to support every TCP state, option, or edge case required by a fully compliant production stack.

## How the stack works

### Outgoing TCP data

1. The TCP engine creates a segment with the appropriate ports, sequence and acknowledgment numbers, flags, window, and payload.
2. The stack calculates the TCP checksum using the IPv4 pseudo-header.
3. IPv4 encapsulates the TCP segment in an IP packet.
4. ARP resolves the destination IP address to a MAC address if necessary.
5. Ethernet wraps the IP packet in a frame.
6. The frame is written to the TAP interface.

### Incoming TCP data

1. The stack reads an Ethernet frame from TAP.
2. Ethernet identifies an IPv4 payload.
3. IPv4 validates and parses the packet.
4. The TCP handler parses the segment and verifies its checksum.
5. The TCP engine matches the segment to a connection or listening port and processes its flags, sequence numbers, acknowledgments, and window information.
6. The engine returns any outgoing segments needed for the connection.
7. Those segments are sent through the IPv4, ARP, and Ethernet layers.

The TCP engine also receives periodic timer ticks from the main event loop to handle retransmission and persist timers.

## Architecture

The main components are organized by responsibility:

| Component | Responsibility |
| --- | --- |
| TAP | Linux virtual Ethernet interface |
| Ethernet | Layer 2 frame parsing and construction |
| ARP | Local IPv4-to-MAC address resolution |
| IPv4 | Network-layer packet parsing and construction |
| ICMPv4 | Echo Request and Echo Reply processing |
| UDP | Connectionless datagram transport |
| TCP segments | TCP header, options, flags, and checksums |
| TCP engine | Connection state, reliability, windows, retransmissions, and congestion control |
| Checksum | Shared Internet checksum implementation |
| Main event loop | Receives frames, dispatches protocols, and advances timers |

## Project structure

```text
TCP-IP-Stack/
├── include/
│   └── net/
│       ├── tap.hpp
│       ├── ethernet.hpp
│       ├── arp.hpp
│       ├── arp_cache.hpp
│       ├── ipv4.hpp
│       ├── icmpv4.hpp
│       ├── udp.hpp
│       ├── tcp.hpp
│       ├── tcp_engine.hpp
│       └── checksum.hpp
├── src/
│   ├── main.cpp
│   └── net/
│       ├── tap.cpp
│       ├── ethernet.cpp
│       ├── arp.cpp
│       ├── arp_cache.cpp
│       ├── ipv4.cpp
│       ├── icmpv4.cpp
│       ├── udp.cpp
│       ├── tcp.cpp
│       ├── tcp_engine.cpp
│       └── checksum.cpp
├── tests/
│   ├── udp_test.cpp
│   ├── tcp_test.cpp
│   ├── tcp_engine_test.cpp
│   ├── tcp_retransmit_test.cpp
│   └── tcp_congestion_test.cpp
├── CMakeLists.txt
└── README.md
```

## Requirements

- Linux with TUN/TAP support.
- A C++20-compatible compiler.
- CMake 3.20 or newer.
- Linux networking utilities such as `ip`.
- Appropriate permissions to create, configure, and access the TAP interface.

On Ubuntu or Debian, install the development tools with:

```bash
sudo apt update
sudo apt install build-essential cmake iproute2
```

## Build

Clone the repository:

```bash
git clone https://github.com/yaminmusleh/TCP-IP-Stack.git
cd TCP-IP-Stack
```

Configure and build all targets:

```bash
cmake -S . -B build
cmake --build build
```

The project uses C++20 and enables common compiler warnings for its targets.

## Run the tests

The project defines separate tests for UDP, TCP segments, TCP connection behavior, retransmissions, and congestion control.

Run all registered tests after building:

```bash
ctest --test-dir build --output-on-failure
```

Individual test executables can also be run directly:

```bash
./build/udp_test
./build/tcp_test
./build/tcp_engine_test
./build/tcp_retransmit_test
./build/tcp_congestion_test
```

A passing unit test verifies the behavior covered by that test. Passing unit tests alone does not establish full protocol compliance or successful end-to-end communication through TAP.

## Running with TAP on Linux

The application currently opens an interface named `tap0` and uses `10.0.0.1` as its stack IP address. Its initial UDP demonstration targets `10.0.0.2`, and its TCP engine listens on port `9000`.

Create the TAP interface if it does not exist:

```bash
sudo ip tuntap add dev tap0 mode tap
```

Bring it up:

```bash
sudo ip link set tap0 up
```

Configure the Linux-side address as `10.0.0.2/24` for the two-endpoint example:

```bash
sudo ip addr add 10.0.0.2/24 dev tap0
```

Verify the interface:

```bash
ip addr show tap0
```

Then start the stack:

```bash
sudo ./build/tcp_stack
```

The userspace stack uses `10.0.0.1`; the Linux-side endpoint uses `10.0.0.2`. These addresses serve different roles in the example and should not be assigned to the same endpoint.

If the interface already exists or has an address configured, inspect its state before repeating the setup commands.

## TCP testing

The TCP engine is configured to listen on port `9000` and uses an echo handler for received application data.

A useful next integration test is to connect from a TCP client on the Linux side, send data, verify that the connection is established, and confirm that the received data is echoed back. Packet captures on `tap0` can help inspect the SYN/SYN-ACK/ACK handshake, data segments, acknowledgments, and connection teardown.

For packet inspection, tools such as `tcpdump` or Wireshark can be useful.

## Current scope and limitations

This is an educational implementation, not a drop-in replacement for the Linux kernel networking stack.

- IPv4 is supported; IPv6 packet processing is currently ignored.
- TCP implements meaningful connection, reliability, window, and congestion-control behavior, but is not claimed to implement every feature and edge case in the TCP specification.
- The TCP engine is designed around a limited number of connections and a simple application data-handler interface.
- The application currently uses a fixed interface name, IP configuration, and listening port.
- The project does not provide a general-purpose POSIX socket API.
- Routing across multiple networks, production-grade resource management, and comprehensive interoperability have not been established.
- End-to-end behavior should be validated separately from unit tests.

## Learning goals

This project is intended to make the following networking concepts tangible:

- Protocol layering and packet encapsulation.
- Ethernet frames, MAC addresses, and ARP resolution.
- IPv4 headers and Internet checksums.
- UDP datagrams versus TCP byte-stream transport.
- TCP connection establishment and teardown.
- Sequence numbers, acknowledgments, and retransmission.
- Receive windows and flow control.
- Congestion windows, slow start, and congestion avoidance.
- Timer-driven protocol behavior and packet-level debugging.

## References

- [Saminiir — Let's Code a TCP/IP Stack](https://www.saminiir.com/lets-code-tcp-ip-stack-1-ethernet-arp/)
- [RFC 826 — Address Resolution Protocol](https://www.rfc-editor.org/rfc/rfc826)
- [RFC 791 — Internet Protocol](https://www.rfc-editor.org/rfc/rfc791)
- [RFC 792 — Internet Control Message Protocol](https://www.rfc-editor.org/rfc/rfc792)
- [RFC 768 — User Datagram Protocol](https://www.rfc-editor.org/rfc/rfc768)
- [RFC 9293 — Transmission Control Protocol](https://www.rfc-editor.org/rfc/rfc9293)
- [RFC 6298 — Computing TCP's Retransmission Timer](https://www.rfc-editor.org/rfc/rfc6298)
- [RFC 5681 — TCP Congestion Control](https://www.rfc-editor.org/rfc/rfc5681)
- [Linux TUN/TAP documentation](https://docs.kernel.org/networking/tuntap.html)


---

The purpose of this project is to learn networking by implementing it layer by layer, from Ethernet frames to transport protocols, and to understand the mechanisms that make packet delivery and reliable communication possible.
