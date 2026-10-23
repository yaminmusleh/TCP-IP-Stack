What does a TCP/IP stack do? 
takes raw data from your applications, breaks it into smaller pieces, adds routing and delivery instructions, and transmits it across a physical network.

We have 4 layers:
 - Application layer
 - Transport layer
 - Internet layer
 - Link layer

TUN/TAP devices: 

how your C++ program will receive and send Ethernet frames without using a physical network card directly.

The important thing is: you don't need to implement TAP yourself. Linux provides it. Your job is to open it and communicate with it.

TAP operates at Layer 2, which is exactly what you want because your stack starts at Layer 2.

A TUN device would give you IP packets (Layer 3): TUN → [IPv4 packet]

A TAP device gives you Ethernet frames: TAP → [Ethernet header][IPv4 packet] so it gives the packet a header, thats why we choose TAP

we need something like this:
┌──────────────────┬──────────────────┬───────────────┐
│ Ethernet header  │ IPv4 header      │ TCP header... │
└──────────────────┴──────────────────┴───────────────┘

-----------------------------------------------------------------------------------------------------------------------------------------------

# First Task:
 - Create a TAP interface and successfully read/write frames from it. 
      we need tap.cpp in src/ and tap.hpp in include/

   Status (Done).


what to implement now:

---------------- A. A TAP device:

 - we need to create/open a TAP device 

 Linux
  │
  │ Ethernet frames
  ▼
tap0
  │
  ▼
your C++ program

we need something like TapDevice responsible for: 
- creating the TAP device
- keeping its file descriptor
- reading frames
- writing frames

----------------- B. Ethernet frame
┌──────────────┬──────────────┬───────────┬─────────────┐
│ dest MAC     │ source MAC   │ EtherType │ payload     │
│ 6 bytes      │ 6 bytes      │ 2 bytes   │ ...         │
└──────────────┴──────────────┴───────────┴─────────────┘

My program should recive: 

raw bytes
   ↓
Ethernet header
   ↓
inspect EtherType

example:

EtherType = 0x0806
            ↓
           ARP

EtherType = 0x0800
            ↓
           IPv4
-----------------------------------------------------------------------------------------------------------------------------------------------

ARP is the first actual protocol we're implementing

ARP answers:

"I know an IP address. What MAC address belongs to it?"
like this:

ARP: Who has 10.0.0.4?

     broadcast
         │
         ▼
   ┌─────────────┐
   │ 10.0.0.4    │
   │             │
   │ MAC = AA:BB │ as i see this MAC has it.
   └─────────────┘
         │
         ▼
"10.0.0.4 is AA:BB:..."

ARP is splitted into two structres:

ARP header
     +
ARP IPv4 data

like this

ARP packet
┌─────────────────────────────┐
│ hardware type               │
│ protocol type               │
│ hardware address length     │
│ protocol address length     │
│ opcode                      │
├─────────────────────────────┤
│ sender MAC                  │here starts the second struct
│ sender IP                   │
│ target MAC                  │
│ target IP                   │
└─────────────────────────────┘

------------ My ARP implementation needs a cache:

Suppose your stack learns:

192.168.1.10 → AA:BB:CC:DD:EE:FF

You don't want to ask:

"Who has 192.168.1.10?"

every single time.

so you maintain something like a table:

ARP table

IP address       MAC address
-----------------------------------
192.168.1.10     AA:BB:CC:DD:EE:FF
192.168.1.20     11:22:33:44:55:66

in conclusion here we need this progress:

Receive Ethernet frame
        │
        ▼
Is it ARP?
        │
       YES
        │
        ▼
Parse ARP
        │
        ▼
Is it for my IP?
        │
       YES
        │
        ▼
Is it an ARP request?
        │
       YES
        │
        ▼
Construct ARP reply
        │
        ▼
Construct Ethernet frame
        │
        ▼
write() → TAP (we send an ethernet frame into TAP device)


there is an instruction in linux IFF_NO_PI 
What does IFF_NO_PI have to do with all this?

- This is simply telling Linux: "When I read from TAP, give me the Ethernet frame itself."

Without IFF_NO_PI, Linux adds a small packet-information header before the actual Ethernet frame.

-----------------------------------------------------------------------------------------------------------------------------------------------

fd_ = open("/dev/net/tun", O_RDWR);

asks Linux:

"Give my program access to the TUN/TAP device."

we did inside the constructor TapDevice in tap.cpp:

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
    name_ = ifr.ifr_name

now what does each of them means?

- ifreq → Linux structure used to configure network interfaces.
- IFF_TAP → tells Linux we want Ethernet frames (Layer 2).
- IFF_NO_PI → don't prepend Linux's extra packet-information header.
- ifr_name → requests the interface name, e.g. tap0.
- TUNSETIFF → tells /dev/net/tun: "turn this file descriptor into this TAP interface."
- name_ = ifr.ifr_name → stores the actual interface name Linux accepted.


Now we can say the TAP device portion has been completed.

-----------------------------------------------------------------------------------------------------------------------------------------------

NEXT STEP: Read a raw Ethernet frame from tap0.

Before ARP, before IP, before TCP — we need to prove our program can actually receive Ethernet frames.

(Note: unsigned char Represents raw bytes of data, binary streams, and small positive numbers. rather than text as char usually provides)

why we will use unsigned char in Ethernet? 
reason: Ethernet frames are fundamentally structured sequences of (raw bytes) when transmitted or handled in computer memory.

ssize_t -> positive: success, negative: an error, zero: end of file or disconnected.

why we chose buffer(1514)?:
because              (Ethernet header = 14 bytes) + (Maximum Ethernet payload = 1500 bytes) = 1514

Our goal till now:

TAP
 ↓
read()
 ↓
raw bytes
 ↓
std::vector<unsigned char>

now my program is able to read raw Ethernet frames.

-----------------------------------------------------------------------------------------------------------------------------------------------

let's make our program understand the 14-byte Ethernet header generically

+-------------------+
| Destination MAC   | 6 bytes
+-------------------+
| Source MAC        | 6 bytes
+-------------------+
| EtherType         | 2 bytes
+-------------------+
| Payload           |
+-------------------+

We want main.cpp to stop knowing byte offsets. Instead:

raw bytes
   ↓
EthernetFrame::parse()
   ↓
destination MAC
source MAC
EtherType
payload

note: for this code
class EthernetFrame
{

public: //building the constructor and getters
    static EthernetFrame parse(const std::vector<unsigned char> &data); // we send data here by reference

    const std::vector<unsigned char> &destinationMac() const;
    const std::vector<unsigned char> &sourceMac() const;
    std::uint16_t etherType() const;
    //these above are considered getters. we will use them in ethernet.cpp

private: //build the setters
    std::vector<unsigned char> destination_mac_;
    std::vector<unsigned char> source_mac_;
    std::uint16_t ether_type_ = 0; // because its 2 bytes = 16bit and uint16_t represents an int of 16bit
};

the private are where we will store the value, and for the public are where we can get the value from.


note:
frame.destination_mac_ =
        std::vector<unsigned char>(data.begin(), data.begin() + 6);

    frame.source_mac_ =
        std::vector<unsigned char>(data.begin() + 6, data.begin() + 12);

This code is extracting the Source and Destination MAC addresses out of the raw incoming byte stream (data) and storing them cleanly inside your frame object.

Raw Data Indices:
[ 0 ][ 1 ][ 2 ][ 3 ][ 4 ][ 5 ] [ 6 ][ 7 ][ 8 ][ 9 ][ 10 ][ 11 ] [ 12 ][ 13 ]
\____________________________/ \_____________________________/  \__________/
      Destination MAC                   Source MAC               EtherType
        (First 6 bytes)               (Next 6 bytes)            (Last 2 bytes)


frame.ether_type_ = (static_cast<std::uint16_t>(data[12]) << 8) | static_cast<std::uint16_t>(data[13]);

this extracts ether type from both 12th and 13th bytes and convert them into useful 16 bit integers.
also for the 12th bit it will shift all bits it to the left by 8 spaces.

-----------------------------------------------------------------------------------------------------------------------------------------------
Let's make our program understand Payload.
last time we made it understand 14 byte ethernet frame (destination mac, source mac, ethertype) in ethernet.hpp and ethernet.cpp

# First of all : what is Payload in ethernet frames?

- the core data section that carries information from higher network layers from one device to another

- the Payload is (everything) after the ethernet frame 14 bytes.

which means that payload will start after the 14th byte so we do data.begin()+14 till data.end()

-----------------------------------------------------------------------------------------------------------------------------------------------
Let's build an ARP parser (Address Resolution Protocol)
- it maps 6 bytes (48 bit) Ethernet MAC address into a protocoladdress (IPV4 Address)

What do we need?

we need to create an ArpPacket

- Hardware type (2 bytes)

- Protocol type (2 bytes)

- Opcode (request/reply) (2 bytes)

- Sender MAC and IP (first device that sends) (6 bytes sender MAC and 4 bytes sender IP)

- Target MAC and IP (second device that receives) (6 bytes target MAC and 4 bytes target IP)

but let's keep in mind my Ethernet frame has EtherType: 0x86dd this is IPV6
but for ARP we need EtherType: 0x0806

now an Ethernet frame with an ARP has:

Ethernet header (14 bytes)
        │
        ▼
ARP header + IPv4 addresses

why do we use uint16_t for the arp packets?
because it matches exactly what we want

since the hardware type, protocol type and op code are 2 bytes each
2 bytes = 16bit
and uint16_t  basically converted 2 bytes into 16 bit integer that we can use

we will see if the data size is less than 8. but why 8?
we need to check if the data returns:

- Hardware type (2 bytes)

- Protocol type (2 bytes)

- Hardware size (1 byte)

- Protocol size (1 byte)

- Opcode (request/reply) (2 bytes)

-------------- Total = 8 bytes.

(NOTE: opcode is the last 2 bytes of this 8 bytes so it takes 6 and 7)

-----------------------------------------------------------------------------------------------------------------------------------------------
# Next step: parse the actual ARP addresses

after the success of detecting ARP requests from the TCP/IP Stack. We now should parse the actual ARP addresses.

we now need to know:
Who is asking?
What IP are they asking about?
What MAC address are they using?

so we will add:
Sender MAC
Sender IP
Target MAC
Target IP

To the ArpPacket

remember in arp.cpp to shift every bit into left, since IP addresses are 32 bit long it will be like this:

00000000 00000000 00000000 00000000

so how is it going to be?  Lets take 192.168.1.1

192 << 24 --> so it will move 24 positions
in Hex its 0xC0 so it will be in this area [00000000] 00000000 00000000 00000000

168 << 16 --> it will move 16 positions making it here: 00000000 [00000000] 00000000 00000000

1 << 8 --> 00000000 00000000 [00000000] 00000000

the last 1 doesn't need to be shifted since its hex and binary is located here : 00000000 00000000 00000000 [00000000]

VERY IMPORTANT NOTE:

Sender Mac and Target Mac are both 6 bytes each. 
Sender IP and Target IP are both 4 bytes which is 16 bits so we cover each with uint32_t (4 bytes) instead of uint16_t (2 bytes)

Why we didn't use uint64_t to hold 6 bytes of mac since it can hold them? the reason is wasted memory space
unsigned char array can hold exactly 6 bytes BUT uint64_t takes 8 bytes. So we need the one that uses Less memory.
Notice we used array<unsigned char> not vector.

------------------------------------------------
MacAddress ArpPacket::senderMac() const 
{
    return sender_mac_;
}
MacAddress ArpPacket::targetMac() const
{
    return target_mac_;
}

in these two no need to write array<unsigned char,6> becuase i already identified it as MacAddress in arp.hpp

now before we continue we need to validate ARP hardware 

so we:
- Add hardware and protocol size fields
- check the sizes if they were not 6 nor 4 reject it, why? because the parser assumes 6 bytes are MAC and 4 bytes are IP
anything that isn't them gets rejected as an invalid ARP frame.


A very important note!!!: hardware in ARP is --> MAC Address
                          Protocol is --> IP
        so thats why we need hardware type and protocol type and their sizes.
        the hardware (MAC) takes data[4] as it's size
        the protocol's size uses data[5]
-----------------------------------------------------------------------------------------------------------------------------------------------
# Next step: Constructing an ARP reply

We need:

- Receiving the Request
- Checking the IP
- Ignoring or Responding
- Building the Reply Packet
- Unicast Transmission
- Updating the Cache

--------- First of them is Receiving the request
previously i did recieve (read) then parse (read) 
now i will do write (reply)

{Quick note: ssize_t is different from size_t, size_t is an unsigned integer meaning it can hold either zero or positive values
while ssize_t means "signed size_t" which indicates that it is a signed integer holding either negative or zero or positive values.}

readFrame() receives raw Ethernet bytes, while writeFrame() will allow us to transmit raw Ethernet bytes. Our ARP reply will eventually be a 42-byte frame:

- Ethernet header: 14 bytes
- ARP payload:     28 bytes
---- Total:   42 bytes ----

┌──────────────────────────────────────────────────────────────────────────────────────┐
│                                  ARP Packet Format                                   │
└──────────────────────────────────────────────────────────────────────────────────────┘

  ┌──────────────────┬──────────────────┬────────────┬───────────────────────────────┐
  │ Ethernet Dest    │ Ethernet Src     │ Frame Type │          ARP Packet           │  ==> 42 bytes total
  │     (6 bytes)    │     (6 bytes)    │ (2 bytes)  │          (28 bytes)           │
  └──────────────────┴──────────────────┴────────────┴───────────────────────────────┘
  └─── Ethernet Header (14 bytes) ───────────────────┘└─── 28 Byte ARP Request/Reply ──┘
                                                           │
                                                           ▼
┌──────────┬──────────┬──────────┬──────────┬────┬──────────────┬──────────┬──────────────┬──────────┐
│ hard type│ prot type│ hard size│ prot size│ op │sendr Eth addr│sender IP │targt Eth addr│target IP │
├──────────┼──────────┼──────────┼──────────┼────┼──────────────┼──────────┼──────────────┼──────────┤
│ 2 bytes  │ 2 bytes  │  1 byte  │  1 byte  │ 2  │   6 bytes    │ 4 bytes  │   6 bytes    │ 4 bytes  │
└──────────┴──────────┴──────────┴──────────┴────┴──────────────┴──────────┴──────────────┴──────────┘

  • frame type is 0x806 for an ARP request or an ARP reply
  • op Code field:
      1 : ARP request
      2 : ARP reply
      3 : RARP request
      4 : RARP reply

-----------------------------------------------------------------------------------------------------------------------------------------------
# Next step: Define an ARP reply builder
== We need a function that takes an incoming ARP request and creates a response.
the reply should swap: 

┌─────────────────┬───────────────────┬───────────────────┐
│ Field           │ ARP Request       │ ARP Reply         │
├─────────────────┼───────────────────┼───────────────────┤
│ Sender MAC      │ Requester's MAC   │ Our MAC           │
│ Sender IP       │ Requester's IP    │ Our IP            │
│ Target MAC      │ Broadcast         │ Requester's MAC   │
│ Target IP       │ Our IP            │ Requester's IP    │
│ Opcode          │ 1 (request)       │ 2 (reply)         │
└─────────────────┴───────────────────┴───────────────────┘

Important: We need to add a serialization method afterward to convert the reply object back into raw bytes. We'll do that in the next step.

note: why did we do this?

    reply.target_mac_ = request.senderMac();
    reply.target_ip_ = request.senderIp();

because The original Target IP becomes the new Sender IP: 
The responding host identifies itself as the owner of that IP address 
and attaches its MAC address in the "Sender Hardware Address" field.
-----------------------------------------------------------------------------------------------------------------------------------------------
# Next Step: Serialize the ARP reply (we want to convert it back into a 28 raw bytes)

i need to convert everything i made in parse back into raw bytes. using unsigned char vectors

the shifts we did to the left will return to be on the right

Why shift the values?
ARP uses network byte order (big-endian).

For example, the IP address 192.168.1.21 becomes: 192 168 1 21

# Now we will use Round-Trip parsing then construct the Ethernet reply

why is it important? for Data Safety. without it, the serialization will work without an error but you can't make sure that this data is safe
if there is a typo in a configuration or a mismatched data type, the deserializer might completely skip fields, leaving them as null or empty. Round-tripping catches this immediately.

## Benefits of round-trip verification:
- Guarantees data integrity and prevents silent data loss.
- Catches floating-point precision issues and timezone shifts early.
- Sets a baseline to prevent regressions during future schema changes.

it's idea is simple: after you serialize data, parse them again.

#### first step: normal parsing for ethernet payload:

ArpPacket arp =
            ArpPacket::parse(ethernet.payload());

#### then call the function of serialization in arp.cpp: 

auto serialized_arp = arp.serialize(); --> here we serialized the ethernet payload bytes.

#### then call parse again from ArpPacket:

ArpPacket parsed_again = ArpPacket::parse(serialized_arp); ---> we didn't use arp.something because we dont need the ethernet payload bytes
we need the serialized bytes so we can parse them that's why i call parse from ArpPacket and not arp

#### then we check if every parsed again arp packet matches the previous parse:

if(parsed_again.hardwareType() == arp.hardwareType() &&
            parsed_again.protocolType() == arp.protocolType() &&
            parsed_again.hardwareSize() == arp.hardwareSize() &&
            parsed_again.protocolSize() == arp.protocolSize() &&
            parsed_again.opCode() == arp.opCode() &&
            parsed_again.senderMac() == arp.senderMac() &&
            parsed_again.targetMac() == arp.targetMac() &&
            parsed_again.senderIp() == arp.senderIp() &&
            parsed_again.targetIp() == arp.targetIp())
        {
            std::cout << "ARP round-trip successful!\n";
        }
        else
        {
            std::cout << "ARP round-trip failed!\n";
        }

-----------------------------------------------------------------------------------------------------------------------------------------------
# Now we construct the ARP reply:

 MacAddress ourMac = {
            0x56, 0x1d, 0x27, 0x4d, 0xbf, 0x83
        };

        std::uint32_t ourIp =
            (10u << 24) |
            (0u << 16) |
            (0u << 8) |
            1u;

        ArpPacket reply =
            ArpPacket::createReply(arp, ourMac, ourIp);

### the createReply is already built in arp.cpp as we remember:

ArpPacket ArpPacket::createReply(const ArpPacket &request,
                                 const MacAddress &ourMac,
                                 std::uint32_t ourIp)
{
    ArpPacket reply;

    reply.hardware_type_ = request.hardwareType();
    reply.protocol_type_ = request.protocolType();

    reply.hardware_size_ = request.hardwareSize();
    reply.protocol_size_ = request.protocolSize();

    reply.op_code_ = 2;

    reply.sender_mac_ = ourMac;
    reply.sender_ip_ = ourIp;

    reply.target_mac_ = request.senderMac();
    reply.target_ip_ = request.senderIp();

    return reply;
}

### Now we build a public method in ethernet.hpp that stores the destination mac, source mac, ether type and payload:

 static std::vector<unsigned char> build(
        const std::vector<unsigned char> &destinationMac,
        const std::vector<unsigned char> &sourceMac,
        std::uint16_t etherType,
        const std::vector<unsigned char> &payload
    );

"build" takes the individual fields you give it, stacks them together in proper network order, and returns a complete, ready-to-send binary frame.

## Implement it in ethernet.cpp:

it will be from type std::vector<unsigned char> rather than EthernetFrame.
we will check firstly if the destinationMac and sourceMac are both 6 bytes. if not then throw an error.

then we declare a frame and preallocate memory space that is 14 + payload size (payload size is not constant, thats why we call its size)

           frame.reserve(14 + payload.size());

now we will use head-to-tail (left-to-right) order
 #### first:
 frame.insert(
        frame.end(), -----> means after the end of this frame insert whatever you want (which will be the source and other things)
        destinationMac.begin(),
        destinationMac.end());

frame.insert(
        frame.end(),
        sourceMac.begin(),
        sourceMac.end());

        lets assume: destinationMac: [00, 1A, 2B, 3C, 4D, 5E]
         and source mac was: [80, FA, 5B, 12, 34, 56]
         then the two inserts above will end up like this:
        [ 00, 1A, 2B, 3C, 4D, 5E, 80, FA, 5B, 12, 34, 56 ] it filled mac destination then after it from the end started with source mac

after adding etherType and payload we will have this layout:

Ethernet frame:
┌─────────────────────┐
│ Destination MAC  6  │
│ Source MAC       6  │
│ EtherType        2  │
│ ARP payload     28  │
└─────────────────────┘
Total: 42 bytes
-----------------------------------------------------------------------------------------------------------------------------------------------

# Transmit the ARP reply

in main.cpp
we do: 
auto replyFrame = EthernetFrame::build(
            destinationMac,
            sourceMac,
            0x0806,
            replyPayLoad);

 tap.writeFrame(replyFrame);

        std::cout<<"ARP Reply Sent!\n";

then in terminal 1: cmake --build build && sudo ./build/tcp_stack

terminal 2: sudo ip link set tap0 up 
then this: sudo tcpdump -i tap0 -e -n -vvv arp

in terminal 3: sudo arping -I tap0 -c 1 10.0.0.1

-----------------------------------------------------------------------------------------------------------------------------------------------
# What we built so far:

                 APPLICATION
                      │
                 OpenCV / apps
                      │
                 TCP API
                      │
                     TCP       ← later
                      │
                     UDP       ← later
                      │
                    IPv4       ← next
                      │
                    ICMP       ← after IPv4
                      │
                  Ethernet     ✓
                      │
                    ARP        ✓
                      │
                    TAP        ✓

-----------------------------------------------------------------------------------------------------------------------------------------------
# Next: IPv4 & ICMPv4

our goal:

Ethernet ✓
   ↓
ARP ✓
   ↓
IPv4       ← we're building this now
   ↓
ICMPv4     ← then build this
   ↓
ping ✓

### We will start with ipv4.hpp, we will create a class with a constructor then getters and setters.

first let us know what are the iov4 fields:

Version
IHL
TOS
Total Length
Identification
Flags
Fragment Offset
TTL
Protocol
Checksum
Source IP
Destination IP
Payload

since ipv4 packet is 32 bit = 4 byte: 

+-------------------------------------------------------------------------------+
|                                    4 bytes                                    |
| 0                                                                          31 |
+---------------+---------------+---------------+-------------------------------+
|    version    | header length |type of service|         total length          |
+---------------+---------------+---------------+---------------+---------------+
|           identification number               |     flags     |fragment offset|  since "identification number" = 16 bit
+-------------------------------+---------------+---------------+---------------+  then the "total length" is 16 bit
|         time to live          |   protocol    |        header checksum        |
+-------------------------------+---------------+-------------------------------+
|                                source address                                 |
+-------------------------------------------------------------------------------+
|                              destination address                              |
+---------------------------------------------------------------+---------------+
|                            options                            |    padding    |
+---------------------------------------------------------------+---------------+
|                                                                               |
|                                     data                                      |
|                                                                               |
+-------------------------------------------------------------------------------+

A couple of important choices:

version → uint8_t, even though it occupies only 4 bits.
headerLength → uint8_t, because we'll extract the 4-bit IHL from the first byte.
flags → uint8_t; we'll extract the 3 relevant bits.
fragmentOffset → uint16_t.
IP addresses → uint32_t, just like my ARP implementation.
payload → vector<unsigned char>, consistent with Ethernet and ARP.

### Now we implement what we did in ipv4.cpp:

why do we see if the data size < 20?
The IPv4 header starts at 20 bytes when there are no options
Later we'll use the IHL field to determine whether the header is actually larger than 20 bytes.

## Now we will parse Version and IHL:

the first byte contains two 4-bit fields: 

              byte 0
┌───────────────┬───────────────┐
│    Version    │      IHL      │
│     4 bits    │    4 bits     │
└───────────────┴───────────────┘
       bits 7-4        bits 3-0
    
so we will add in ipv4.cpp under Ipv4Packet packet; :

packet.version_ = data[0] >> 4;
packet.header_length_ = data[1] & 0x0F;

the first data[0] >> 4 does: moves the upper 4 bits down:

1010 0101
^^^^
1010

the second one: data[0] & 0x0F keeps only the lower 4 bits:

1010 0101
     ^^^^
     0101

then we need validation: 

if (packet.version_ != 4)
{
    throw std::runtime_error("Not an IPv4 packet");
}

if (packet.header_length_ != 5)
{
    throw std::runtime_error(
        "IPv4 options are not supported");
}

### Now we parse "type of service (tos)"

packet.tos_ = data[1];

notice that we don't need bit manipulation because the entire byte belongs to that field.


## Starting now with the total length:
Total Length is the first 16-bit IPv4 field we're parsing.
it occupies byte 2 and 3

byte 2                  byte 3
┌──────────────────────────────┐
│        Total Length          │
│           16 bits            │
└──────────────────────────────┘

Like ARP, IPv4 stores multi-byte numbers in network byte order (big-endian).

We add this to ipv4.cpp:

packet.total_length_ = (static_cast<std::uint16_t>(data[2]) << 8) | // the OR (|) sums the bits
                           (static_cast<std::uint16_t>(data[3]));

## Now identification which is also 16 bit:
it occupies byte 4 and 5:

byte 4             byte 5
┌──────────────────────────┐
│      Identification      │
│          16 bits         │
└──────────────────────────┘

so we do the same:

packet.identification_ =
    (static_cast<std::uint16_t>(data[4]) << 8) |
    static_cast<std::uint16_t>(data[5]);

## Now we will return to bit extraction again in Flags + Fragment offset.
They both share bytes 6 and 7:

              16 bits
┌─────┬─────────────────────────┐
│Flags│    Fragment Offset      │
│3 bit│        13 bits          │
└─────┴─────────────────────────┘
 bits 15-13          bits 12-0


First we will combine them into one single 16 bit integer:
we make a variable of type uint16_t:

std::uint16_t flagsAndOffset = (static_cast<std::uint16_t>(data[6])) |
                                   (static_cast<std::uint16_t>(data[7]));

then we extract the flag, we do this by moving the flagsAndOffset 13 positions toward left:
and since the flag is 3 bits we use uint8_t:

packet.flags_ = static_cast<std::uint8_t>(flagsAndOffset >> 13);

why >> 13?

flagsAndOffset:

FFF OOOOOOOOOOOOO
^^^

as we see we moved 13 bits to the left.

then we extract the fragment offset:

packet.fragment_offset_ = flagsAndOffset & 0x1FFF;  the 0x1FFF is 13 bits, as if im telling it to choose the first 13 bits with (&)

-----------------------------------------------------------------------------------------------------------------------------------------------
## Next: TTL and Protocol:
These are both simple 1-byte fields.

byte 8       byte 9
┌────────┐   ┌──────────┐
│  TTL   │   │ Protocol │
│ 8 bits │   │ 8 bits   │
└────────┘   └──────────┘
so we add:

packet.ttl_ = data[8];
packet.protocol_ = data[9];

to our ipv4.cpp

### What do they mean?

TTL: Time To Live: 
Used to prevent packets from circulating forever. Routers decrement it as the packet travels. If it reaches zero, the packet is discarded.

Protocol: Tells IPv4 what protocol is carried inside the packet:
e.g. 

1   = ICMP
6   = TCP
17  = UDP

-----------------------------------------------------------------------------------------------------------------------------------------------
## Next: Header checksum:
The checksum occupies bytes 10–11:

byte 10                byte 11
┌───────────────────────────┐
│      Header Checksum      │
│          16 bits          │
└───────────────────────────┘

packet.checksum_ =
    (static_cast<std::uint16_t>(data[10]) << 8) |
    static_cast<std::uint16_t>(data[11]);


Note: Later we will implement its algorithm.

## Next: Source IP address parsing.
It occupies bytes 12–15 (12 to 15):

byte 12        byte 13        byte 14        byte 15
┌──────────┬──────────┬──────────┬──────────┐
│           Source IP Address               │
│                  32 bits                  │
└──────────┴──────────┴──────────┴──────────┘
We're storing IP addresses as a std::uint32_t, just like we did with sender_ip_ in ARP:

packet.source_ip_ =
    (static_cast<std::uint32_t>(data[12]) << 24) |
    (static_cast<std::uint32_t>(data[13]) << 16) |
    (static_cast<std::uint32_t>(data[14]) << 8) |
    static_cast<std::uint32_t>(data[15]);

so if the bytes are: 10.0.0.1 then they are: 0A 00 00 01 in HEX and the expression produces: 0x0A000001

This is exactly the same byte-order approach I already used in my ARP parser, the difference is that here we're filling source_ip_.

## Then we'll do the exact same thing for Destination IP at bytes 16–19.

byte 16        byte 17        byte 18        byte 19
┌──────────┬──────────┬──────────┬──────────┐
│         Destination IP Address            │
│                  32 bits                  │
└──────────┴──────────┴──────────┴──────────┘

big endian:

packet.destination_ip_ =
    (static_cast<std::uint32_t>(data[16]) << 24) |
    (static_cast<std::uint32_t>(data[17]) << 16) |
    (static_cast<std::uint32_t>(data[18]) << 8) |
    static_cast<std::uint32_t>(data[19]);

for now the ipv4 header is almost complete:

Bytes   Field
0       Version + IHL
1       TOS
2-3     Total Length
4-5     Identification
6-7     Flags + Fragment Offset
8       TTL
9       Protocol
10-11   Header Checksum
12-15   Source IP
16-19   Destination IP

before we continue with the payload, let us add the checksum algorithm.

## Next: The checksum algorithm:
IPv4 uses a 16-bit one's-complement checksum.

how does it work (Very Simple):

Treat the header as 16-bit words.
Add all the words together.
If there is a carry beyond 16 bits, add that carry back into the lower 16 bits.
Take the one's complement (~) of the result.

like this:

word 1
+ word 2
+ word 3
+ ...
---------
sum

then checksum = ~sum;

For IPv4, the checksum field itself must be treated as zero while calculating the checksum.

So we will make helper:

std::uint16_t internetChecksum(const std::vector<unsigned char> &data)
{
    std::uint32_t sum = 0;

    for (std::size_t i = 0; i < data.size(); i += 2)
    {
        std::uint16_t word =
            static_cast<std::uint16_t>(data[i]) << 8;

        if (i + 1 < data.size())
        {
            word |= data[i + 1]; // this is a bitwise or operation (word = word | data[i+1])
        }

        sum += word;

        if (sum > 0xFFFF)
        {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
    }
    
    return static_cast<std::uint16_t>(~sum);
}

One important detail
For calculating an IPv4 checksum, bytes 10 and 11 must be zero.
So eventually, when we create an outgoing packet, we'll do something like:

IPv4 header
     ↓
checksum field = 0
     ↓
calculate checksum
     ↓
put checksum into bytes 10-11

For validating an incoming packet, we can run the checksum over the complete header including its existing checksum and expect the one's-complement result to indicate a valid header.

-- our helper currently calculates the checksum over whatever bytes we give it. For an incoming IPv4 header, the checksum field is already populated, so we'll validate the header by calculating over the entire header, including the checksum.

## Step 1 — determine the header size
we already have : packet.header_length_ now we want the size after it

std::size_t headerSize =
    static_cast<std::size_t>(packet.header_length_) * 4;

this converts and scales a network/protocol packet header length (which is measured in 32-bit "words") into the exact number of bytes (8-bit units) it occupies in memory. (from bits to bytes)
then the validation:

if (data.size() < headerSize)
{
    throw std::runtime_error(
        "IPv4 packet is smaller than its header");
}
This is important because a packet could technically be 20 bytes long while claiming an IHL of 6, which means its header should actually be 24 bytes.

## Step 2 — validate the checksum

std::vector<unsigned char> header(
    data.begin(),
    data.begin() + headerSize);

if (internetChecksum(header) != 0)
{
    throw std::runtime_error(
        "Invalid IPv4 header checksum");
}

the flow is like this:

raw IPv4 packet
       ↓
read IHL
       ↓
calculate header size
       ↓
extract header
       ↓
calculate checksum
       ↓
must equal 0

## Note: We are validating the checksum before parsing the payload, which is exactly what we want.

### Next step here is the length validation:
since we now know the total length and the header size. WE MUST VALIDATE TOTAL LENGTH
We need to make sure the packet isn't claiming an impossible length.

    if (packet.total_length_ < headerSize)
    {
        throw std::runtime_error("IPV4 total length is smaller than header");
    }
    if (data.size() < packet.total_length_)
    {
        throw std::runtime_error("IPV4 packet is truncated");
    }

first we checked if the length is smaller than the header (because total length must never be less than the header size)
then we checked if the data size is less than the header (the data size is the amount of bytes we recieved from the Ethernet)
the data size should not be less than total length

so:

             total_length_
        ┌─────────────────────┐
        │                     │
        │   IPv4 Header       │
        │                     │
        ├─────────────────────┤
        │                     │
        │   Payload           │
        │                     │
        └─────────────────────┘
        ↑                     ↑
        0              total_length_

        headerSize
        ├──────────────┤
        IPv4 Header
then we check if the total length is zero (it must never be zero):

if (packet.total_length_ == 0)
{
    throw std::runtime_error(
        "Invalid IPv4 total length");
}
(its not necessary, the previous already does it so we won't add it)

-----------------------------------------------------------------------------------------------------------------------------------------------
# PayLoad:

## 1. Calculate the payload size
the payload starts immediately after the ipv4 header.

┌──────────────────┬──────────────────────┐
│   IPv4 Header    │       Payload        │
│    headerSize    │ total_length-header  │
└──────────────────┴──────────────────────┘
                    ↑
                payload starts
            
std::size_t payloadSize = packet.total_length_ - headerSize; to calculate the size of the payload.

## Extract the payload
we will use assign() with payload because its an already full vector
assing() replaces the old elements with new ones.

packet.payload_.assign(data.begin() + headerSize, data.begin() + packet.total_length_);
this means:

data.begin() + headerSize
        ↓
        payload starts

data.begin() + total_length_
        ↓
        payload ends

#### One small note: payloadSize isn't actually needed by assign(). We can keep it for clarity while learning, but the compiler may warn that it's unused because of -Wall -Wextra.

My parser now is able to do:

Raw IPv4 bytes
     │
     ├── minimum 20 bytes?
     │
     ├── Version + IHL
     │
     ├── Version == 4?
     │
     ├── Header size valid?
     │
     ├── TOS
     ├── Total Length
     ├── Identification
     ├── Flags + Fragment Offset
     ├── TTL
     ├── Protocol
     ├── Header Checksum
     ├── Source IP
     ├── Destination IP
     │
     ├── checksum valid?
     │
     ├── total length valid?
     │
     └── extract payload

-----------------------------------------------------------------------------------------------------------------------------------------------
# Let's Begin with Serialize(): (note: its different from serialize in arp.cpp)
Since we're intentionally keeping this implementation minimal, we'll serialize a 20-byte IPv4 header with no options

so the total packet size is = 20-byte header + payload size

std::vector<unsigned char> Ipv4Packet::serialize() const
    {
        std::vector<unsigned char> data(20 + payload_.size());

        return data;
    }

but we should make sure to not put it inside parse in ipv4.cpp, we but it as an independent function under it.
the vector is initialized to zero:
┌────────────────────┬──────────────────────┐
│    20-byte header  │       payload        │
└────────────────────┴──────────────────────┘

One important thing: my header length should be 5 because: 5 * 4 = 20 bytes
and total length is 20 + payload size

### Now we can build serialize() incrementally.

## Step 1 — Version + IHL:
The first IPv4 byte contains two 4-bit fields as we discussed above:

bits 7-4: Version
bits 3-0: IHL

to get the first byte we should do a bitwise ORing between the version we receive and the IHL (header length)

so our serialize inside ipv4.cpp becomes: 

std::vector<unsigned char> Ipv4Packet::serialize() const
{
    std::vector<unsigned char> data(20 + payload_.size());

    data[0]=static_cast<unsigned char>((version_ <<4) | header_length_);

    return data;
}

how does it work? 

e.g.

version_ = 4
header_length_ = 5

4 << 4 = 0100 0000
5      = 0000 0101
-----------------
         0100 0101 = 0x45

0x45 is the normal first byte of a minimal IPv4 header.

## Step 2 — TOS + Total Length

we add this inside the serialize under data[0]

data[1] = tos_;

data[2] =
    static_cast<unsigned char>(total_length_ >> 8);

data[3] =
    static_cast<unsigned char>(total_length_ & 0xFF);

so:

std::vector<unsigned char> Ipv4Packet::serialize() const
{
    std::vector<unsigned char> data(20 + payload_.size());

    data[0] = static_cast<unsigned char>((version_ << 4) | header_length_);

    data[1] = tos_;

    data[2] = static_cast<unsigned char>(total_length_ >> 8);

    data[3] = static_cast<unsigned char>(total_length_ & 0xFF);

    return data;
}

Why split total_length_?

total_length_ is a 16-bit value, but the packet stores it as two bytes in network byte order (big-endian):

total_length_ = 0x1234
it will be:
data[2] = 0x12
data[3] = 0x34

## Step 3 — Identification + Flags/Fragment Offset:
all we got to do is adding these inside the serialize():

data[4] =
    static_cast<unsigned char>(identification_ >> 8);

data[5] =
    static_cast<unsigned char>(identification_ & 0xFF);

std::uint16_t flagsAndOffset =
    static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(flags_) << 13) |
        fragment_offset_);

data[6] =
    static_cast<unsigned char>(flagsAndOffset >> 8);

data[7] =
    static_cast<unsigned char>(flagsAndOffset & 0xFF);

IPV4 packs both flags and fragment offset together:

16 bits total

FFF OOOOOOOOOOOOO
^^^ ^^^^^^^^^^^^^
 |       |
flags    fragment offset

flags are 3 bits so: flags_ <<13 then we did | fragment_offset_ this will put the offset underneath them.

### Step 4 — TTL + Protocol:
it will be simply:

data[8] = ttl_;
data[9] = protocol_;

The protocol tells the ipv4 what's inside the payload.

### Step 5 — Source and Destination IP:

Each IP is stored internally as a std::uint32_t, but the IPv4 header needs 4 separate bytes.
it will be similar to what we disucsed above:

data[12] =
    static_cast<unsigned char>(source_ip_ >> 24);

data[13] =
    static_cast<unsigned char>((source_ip_ >> 16) & 0xFF);

data[14] =
    static_cast<unsigned char>((source_ip_ >> 8) & 0xFF);

data[15] =
    static_cast<unsigned char>(source_ip_ & 0xFF);

data[16] =
    static_cast<unsigned char>(destination_ip_ >> 24);

data[17] =
    static_cast<unsigned char>((destination_ip_ >> 16) & 0xFF);

data[18] =
    static_cast<unsigned char>((destination_ip_ >> 8) & 0xFF);

data[19] =
    static_cast<unsigned char>(destination_ip_ & 0xFF);

but what make this unique that it is a reverse of what we did in parse(). how?

for example: source_ip_ = 0xC0A80115
the bytes become: C0 A8 01 15
which corresponds to: 192.168.1.21

so till now we have: 

0   Version/IHL
1   TOS
2-3 Total Length
4-5 Identification
6-7 Flags/Fragment Offset
8   TTL
9   Protocol
12-15 Source IP
16-19 Destination IP

we will work next with 10-11 bytes which is the checksum

### Step 6 — Calculate the IPv4 checksum:

first we will put 10 and 11 as zeroes:

data[10] = 0;
data[11] = 0;

then we will calculate the checksum over the entire 20 byte header.

std::vector<unsigned char> header(data.begin(), data.begin() + 20);

std::uint16_t checksum = internetChecksum(header);

lets not forget in the internetChecksum() we calculate the checksum in a function immediately instead of putting it's code again and again.

then we write the resulting 16 bit checksum back to the data[10] and data[11]:

data[10] =
    static_cast<unsigned char>(checksum >> 8);

data[11] =
    static_cast<unsigned char>(checksum & 0xFF);

why did we start at zero?
If we included an old checksum while calculating the new checksum, we'd be feeding the checksum into its own calculation.
we temporarily make it: [ ... ][ 00 00 ][ ... ] instead of [ ... ][ checksum ][ ... ]

our parse() does the opposite as we said.

### Lastly: Step 7 — Copy the payload:
we need in this step to add #include<algorithm>

then add this to the serialize():

std::copy(
    payload_.begin(),
    payload_.end(),
    data.begin() + 20);


Our Ipv4Packet can now:

- Parse the IPv4 header
- Validate Version = 4
- Reject IPv4 options for our minimal implementation
- Parse IHL
- Parse total length
- Parse identification
- Parse flags + fragment offset
- Parse TTL
- Parse protocol
- Parse checksum
- Parse source/destination IPs
- Validate the IPv4 checksum
- Extract the payload
- Serialize all IPv4 header fields
- Calculate the IPv4 checksum
- Serialize the payload

-----------------------------------------------------------------------------------------------------------------------------------------------
# Next: ICMPv4: we will create icmpv4.cpp and icmpv4.hpp

## Our first goal is the icmpv4 header:

0       1       2       3
+-------+-------+-------+
| Type  | Code  | Checksum
+-------+-------+-------+
|        Payload        |
+-----------------------+

it uses the same internetChecksum method i built in ipv4 but the difference is that here it covers: ICMP header + ICMP payload

so we need to make the checksum reusable so IPV4 and ICMPV4 can use it without rebuilding.
we need:
checksum.hpp
checksum.cpp

after building them we return to icmpv4.
we will create the generic base header which is 4 bytes.

0       7 8      15 16                     31
+---------+---------+-----------------------+
|  Type   |  Code   |      Checksum         |
+---------+---------+-----------------------+
|              ICMP payload ...             |
+-------------------------------------------+

an echo request is totally 8 bytes but the header that we will create is only 4 bytes

| Field     |        Size |
| --------- | ----------: |
| Type      |      1 byte |
| Code      |      1 byte |
| Checksum  |     2 bytes |
| **Total** | **4 bytes** |

(Note: the checksum for data <MUST> be zero)

a real ping echo request is:

ICMP header
┌──────┬──────┬──────────┐
│ Type │ Code │ Checksum │  4 bytes
└──────┴──────┴──────────┘

Echo data
┌────────────┬────────────┬──────────────┐
│ Identifier │ Sequence   │ Data         │
│  2 bytes   │  2 bytes   │ variable     │
└────────────┴────────────┴──────────────┘
Our generic Icmpv4Packet doesn't yet know that the payload is an Echo packet. It simply treats everything after the 4-byte ICMP header as payload

so the reult will be in icmpv4.cpp:

#include "net/icmpv4.hpp"
#include "net/checksum.hpp"
#include <vector>
#include <stdexcept>

Icmpv4Packet Icmpv4Packet::parse(const std::vector<unsigned char> &data)
{

    if (data.size() < 4)
    {
        throw std::runtime_error("ICMP packet is too small");
    }

    Icmpv4Packet packet;

    packet.type_ = data[0];

    packet.code_ = data[1];

    packet.checksum_ = (static_cast<std::uint16_t>(data[2]) << 8) |
                       (static_cast<std::uint16_t>(data[3]));

    if (internetChecksum(data) != 0)
    {
        throw std::runtime_error("Invalid ICMPv4 checksum");
    }

    packet.payload_.assign(
        data.begin() + 4,
        data.end());

    return packet;
}

then after it we add the serialization:

std::vector<unsigned char> Icmpv4Packet::serialize() const
{
    std::vector<unsigned char> data(
        4 + payload_.size());

    data[0] = type_;
    data[1] = code_;

    data[2] = 0;
    data[3] = 0;

    std::copy(
        payload_.begin(),
        payload_.end(),
        data.begin() + 4);

    std::uint16_t checksum =
        internetChecksum(data);

    data[2] =
        static_cast<unsigned char>(checksum >> 8);

    data[3] =
        static_cast<unsigned char>(checksum & 0xFF);

    return data;
}

Notice the important difference from IPv4:

- For IPv4, the checksum covers the 20-byte IPv4 header.

- For ICMP, the checksum covers: ICMP header + ICMP payload

so here: std::uint16_t checksum =
    internetChecksum(data);

we calculate it after copying the payload.

-----------------------------------------------------------------------------------------------------------------------------------------------
# Next: ICMP Echo Request/Reply (the packets used by ping)

## Step 1 — add Echo fields
an ICMP echo message has:

ICMP header
├── Type       1 byte
├── Code       1 byte
└── Checksum   2 bytes

Echo data
├── Identifier 2 bytes
├── Sequence   2 bytes
└── Data       variable

so we need identifier and sequence in icmpv4.hpp

so our result will be:

#pragma once

#include <cstdint>
#include <vector>

class Icmpv4Packet
{
public:
    static Icmpv4Packet parse(const std::vector<unsigned char> &data);

    std::vector<unsigned char> serialize() const;

    // getters
    std::uint8_t type() const;
    std::uint8_t code() const;
    std::uint16_t checksum() const;
    std::vector<unsigned char> &payload() const;
    std::uint16_t identifier() const;
    std::uint16_t sequence() const;

    // setters
private:
    std::uint8_t type_ = 0;
    std::uint8_t code_ = 0;
    std::uint16_t checksum_ = 0;
    std::uint16_t identifier_ = 0;
    std::uint16_t sequence_ = 0;
    std::vector<unsigned char> payload_;
};

One important detail: not every ICMP packet has identifier and sequence. Those fields belong specifically to Echo Request/Reply. So we'll only read them when the type is 8 (Echo Request) or 0 (Echo Reply).

in parse after validating the checksum:

if (packet.type_ == 8 || packet.type_ == 0)
{
    if (data.size() < 8)
    {
        throw std::runtime_error(
            "ICMP Echo packet is too small");
    }

    packet.identifier_ =
        (static_cast<std::uint16_t>(data[4]) << 8) |
        static_cast<std::uint16_t>(data[5]);

    packet.sequence_ =
        (static_cast<std::uint16_t>(data[6]) << 8) |
        static_cast<std::uint16_t>(data[7]);

    packet.payload_.assign(
        data.begin() + 8,
        data.end());
}
else
{
    packet.payload_.assign(
        data.begin() + 4,
        data.end());
}

so our parser will be:

Icmpv4Packet Icmpv4Packet::parse(const std::vector<unsigned char> &data)
{

    if (data.size() < 4)
    {
        throw std::runtime_error("ICMP packet is too small");
    }

    Icmpv4Packet packet;

    packet.type_ = data[0];

    packet.code_ = data[1];

    packet.checksum_ = (static_cast<std::uint16_t>(data[2]) << 8) |
                       (static_cast<std::uint16_t>(data[3]));

    if (internetChecksum(data) != 0)
    {
        throw std::runtime_error("Invalid ICMPv4 checksum");
    }

    if (packet.type_ == 8 || packet.type_ == 0)
    {
        if (data.size() < 8)
        {
            throw std::runtime_error(
                "ICMP Echo packet is too small");
        }
        
        packet.identifier_ =
            (static_cast<std::uint16_t>(data[4]) << 8) |
            static_cast<std::uint16_t>(data[5]);

        packet.sequence_ =
            (static_cast<std::uint16_t>(data[6]) << 8) |
            static_cast<std::uint16_t>(data[7]);

        packet.payload_.assign(
            data.begin() + 8,
            data.end());
    }

    else
    {
        packet.payload_.assign(
            data.begin() + 4,
            data.end());
    }

    return packet;
}

we add serialization:

std::vector<unsigned char> Icmpv4Packet::serialize() const
{
    bool isEcho =
        (type_ == 8 || type_ == 0);

    std::size_t headerSize = isEcho ? 8 : 4;

    std::vector<unsigned char> data(
        headerSize + payload_.size());

    data[0] = type_;
    data[1] = code_;

    // Checksum must be zero while calculating it.
    data[2] = 0;
    data[3] = 0;

    if (isEcho)
    {
        data[4] =
            static_cast<unsigned char>(identifier_ >> 8);

        data[5] =
            static_cast<unsigned char>(identifier_ & 0xFF);

        data[6] =
            static_cast<unsigned char>(sequence_ >> 8);

        data[7] =
            static_cast<unsigned char>(sequence_ & 0xFF);
    }

    std::copy(
        payload_.begin(),
        payload_.end(),
        data.begin() + headerSize);

    std::uint16_t checksum =
        internetChecksum(data);

    data[2] =
        static_cast<unsigned char>(checksum >> 8);

    data[3] =
        static_cast<unsigned char>(checksum & 0xFF);

    return data;
}

then we add getters in icmpv4.cpp:

std::uint8_t Icmpv4Packet::type() const
{
    return type_;
}

std::uint8_t Icmpv4Packet::code() const
{
    return code_;
}

std::uint16_t Icmpv4Packet::checksum() const
{
    return checksum_;
}

std::uint16_t Icmpv4Packet::identifier() const
{
    return identifier_;
}

std::uint16_t Icmpv4Packet::sequence() const
{
    return sequence_;
}

const std::vector<unsigned char> &Icmpv4Packet::payload() const
{
    return payload_;
}

## Next we will connect ICMP with IPv4:

Ethernet
   │
   ├── ARP → existing ARP handling
   │
   └── IPv4
        │
        └── ICMP
             │
             └── Echo Request → Echo Reply

The first change is to stop treating every non-ARP frame as something to ignore

we want:
EtherType 0x0806 → ARP
EtherType 0x0800 → IPv4
anything else    → ignore

for now we do:

if (ethernet.etherType() == 0x0800) // checking ipv4 frames
{
    std::cout << "Received an IPv4 frame\n";
    Ipv4Packet ipv4 = Ipv4Packet::parse(ethernet.payload());
    std::cout << "IPv4 packet received.\n";
    continue;
}

if (ethernet.etherType() != 0x0806)
{
    std::cout << "Not an ARP or IPv4. Ignoring.\n";
    continue;
}

In ipv4.cpp i changed the header length != 5 to <5

if (packet.header_length_ < 5)
    {
        throw std::runtime_error(
            "Invalid IPv4 header length");
    }

## Next small step: inspect the IPv4 packet
we will replace this:
if (ethernet.etherType() == 0x0800)
{
    std::cout << "Received an IPv4 frame\n";

    Ipv4Packet ipv4 =
        Ipv4Packet::parse(ethernet.payload());

    std::cout << "IPv4 packet received.\n";

    continue;
}

with:
if (ethernet.etherType() == 0x0800)
{
    std::cout << "Received an IPv4 frame\n";

    Ipv4Packet ipv4 =
        Ipv4Packet::parse(ethernet.payload());

    std::cout << "IPv4 packet received.\n";

    std::cout << "Source IP: "
              << ipv4.sourceIp()
              << "\n";

    std::cout << "Destination IP: "
              << ipv4.destinationIp()
              << "\n";

    std::cout << "Protocol: "
              << static_cast<int>(ipv4.protocol())
              << "\n";

    continue;
}

terminal commands:
terminal 1:
cmake --build build
sudo ./build/tcp_stack

terminal 2:
sudo ip link set tap0 up
sudo ip route add 10.0.0.0/24 dev tap0

terminal 3:
ping -c 1 10.0.0.1


## Next step: parse the ICMP packet

in main.cpp:

add this -> #include "net/icmpv4.hpp" 
then inside the ipv4 block after:

std::cout << "Protocol: "
          << static_cast<int>(ipv4.protocol())
          << "\n";

add this:
if (ipv4.protocol() == 1)
{
    Icmpv4Packet icmp =
        Icmpv4Packet::parse(ipv4.payload());

    std::cout << "ICMP packet received.\n";

    std::cout << "ICMP type: "
              << static_cast<int>(icmp.type())
              << "\n";

    std::cout << "ICMP code: "
              << static_cast<int>(icmp.code())
              << "\n";
}

## Now we can build the Echo Reply.

### Step 1 — add a factory for the reply.
we will return to icmpv4,hpp and add this to the public:

     static Icmpv4Packet createEchoReply(
        const Icmpv4Packet &request);

then in the icmpv4.cpp we add this before serialization:

Icmpv4Packet Icmpv4Packet::createEchoReply(const Icmpv4Packet &request)
{

    Icmpv4Packet reply;

    reply.type_ = 0;
    reply.code_ = 0;

    reply.identifier_ = request.identifier_;
    reply.sequence_ = request.sequence_;

    reply.payload_ = request.payload_;

    return reply;
}

an Echo request is like this:

Type        8
Code        0
Identifier  X
Sequence    Y
Data        ...

and after this function it becomes like this:

Type        0
Code        0
Identifier  X
Sequence    Y
Data        ...

The identifier, sequence number, and payload must be preserved so ping can match the reply to the request.
no need for ICMP checksum because serialize() calculates it automatically

## Next we'll build the IPv4 reply.
### Step 2 — add an IPv4 reply factory
almost the same thing as before:

static Ipv4Packet createReply(
    const Ipv4Packet &request,
    const std::vector<unsigned char> &payload);

here we need the payload to be given to the ipv4.

then in ipv4.cpp before serialization:

Ipv4Packet Ipv4Packet::createReply(const Ipv4Packet &request, const std::vector<unsigned char> &payload)
{
    Ipv4Packet reply;

    reply.version_ = 4;
    reply.header_length_ = 5;

    reply.tos_ = request.tos_;

    reply.total_length_ =
        static_cast<std::uint16_t>(20 + payload.size());

    reply.identification_ = 0;
    reply.flags_ = 0;
    reply.fragment_offset_ = 0;

    reply.ttl_ = 64;

    reply.protocol_ = request.protocol_;
    reply.source_ip_ = request.destination_ip_; --> importnat
    reply.destination_ip_ = request.source_ip_; --> important

    reply.payload_ = payload;

    return reply;
}

the important transformation:

Request:
    source      = 192.168.1.21
    destination = 10.0.0.1

Reply:
    source      = 10.0.0.1
    destination = 192.168.1.21

- The packet construction pieces are now in place.

## Step 3 — connect ICMP + IPv4 in main.cpp

we will replace:
if (ipv4.protocol() == 1)
{
    Icmpv4Packet icmp =
        Icmpv4Packet::parse(ipv4.payload());

    std::cout << "ICMP packet received.\n";

    std::cout << "ICMP type: "
              << static_cast<int>(icmp.type())
              << "\n";

    std::cout << "ICMP code: "
              << static_cast<int>(icmp.code())
              << "\n";
}

with:

if (ipv4.protocol() == 1)
{
    Icmpv4Packet icmp =
        Icmpv4Packet::parse(ipv4.payload());

    std::cout << "ICMP packet received.\n";

    std::cout << "ICMP type: "
              << static_cast<int>(icmp.type())
              << "\n";

    std::cout << "ICMP code: "
              << static_cast<int>(icmp.code())
              << "\n";

    if (icmp.type() == 8 && icmp.code() == 0)
    {
        Icmpv4Packet replyIcmp =
            Icmpv4Packet::createEchoReply(icmp);

        auto replyIcmpPayload =
            replyIcmp.serialize();

        Ipv4Packet replyIpv4 =
            Ipv4Packet::createReply(
                ipv4,
                replyIcmpPayload);

        auto replyIpv4Payload =
            replyIpv4.serialize();

        std::cout << "ICMP Echo Reply created.\n";
    }
}

so the flow becomes:

Ethernet frame
      ↓
IPv4 packet
      ↓
ICMP Echo Request
      ↓
createEchoReply()
      ↓
ICMP serialize()
      ↓
IPv4 createReply()
      ↓
IPv4 serialize()

current progress:

Ethernet RX
    ↓
ARP request → ARP reply       ✅
    ↓
IPv4 RX                       ✅
    ↓
ICMP Echo Request             ✅
    ↓
ICMP Echo Reply construction  ✅
    ↓
IPv4 Reply construction       ✅
    ↓
Ethernet TX                 ← next

## Step 4 — send the IPv4 reply:
inside our:
if (icmp.type() == 8 && icmp.code() == 0)

we have: 

Ipv4Packet replyIpv4 =
    Ipv4Packet::createReply(
        ipv4,
        replyIcmpPayload);

auto replyIpv4Payload =
    replyIpv4.serialize();

std::cout << "ICMP Echo Reply created.\n";

after this we do:

 auto replyFrame = EthernetFrame::build(
                    ethernet.sourceMac(), // because we are replying to the source of the request
                    ethernet.destinationMac(),
                    0x0800,
                    replyIpv4Payload);

tap.writeFrame(replyFrame);
                    
std::cout << "ICMP Echo Reply sent.\n";

we switched between source and destination in replying process.

why?

the incoming frame is:

my stack       Linux
    ↓              ↑
source MAC     destination MAC

for the reply we want:

Linux            my stack
    ↓                ↑
source MAC       destination MAC

And 0x0800 means IPv4.

when we activate terminal prompts we should see something like:

--- 10.0.0.1 ping statistics ---
1 packets transmitted, 1 received, 0% packet loss, time 0ms
rtt min/avg/max/mdev = 0.473/0.473/0.473/0.000 ms

and it should give also something like:

ARP Reply Sent!
Received an IPv4 frame
...
ICMP type: 8
ICMP code: 0
ICMP Echo Reply created.
ICMP Echo Reply sent.
...

my own stack can now do:

ARP Request
    ↓
ARP Reply Sent
    ↓
IPv4 Echo Request
    ↓
ICMP type 8 / code 0
    ↓
ICMP Echo Reply created
    ↓
IPv4 Reply
    ↓
Ethernet Reply
    ↓
TAP
    ↓
Linux ping receives it

At this point my stack has:

TAP device — Layer 2 interface
Ethernet — frame parsing/building
ARP — request parsing + replies
IPv4 — parsing, validation, serialization, checksum
ICMPv4 — Echo Request parsing + Echo Reply generation
Internet checksum — shared by IPv4/ICMP
CMake build system
A working end-to-end ping

my architecture will be:

             My TCP/IP Stack
┌─────────────────────────────────────────┐
│              Application                │
│                 ping                    │
└───────────────────┬─────────────────────┘
                    │
┌───────────────────▼─────────────────────┐
│                 ICMP                    │
│       Echo Request / Echo Reply         │
└───────────────────┬─────────────────────┘
                    │
┌───────────────────▼─────────────────────┐
│                 IPv4                    │
│    header parsing / checksum / routing  │
└───────────────────┬─────────────────────┘
                    │
┌───────────────────▼─────────────────────┐
│               Ethernet                  │
│          frame parsing/building         │
└───────────────────┬─────────────────────┘
                    │
┌───────────────────▼─────────────────────┐
│                 ARP                     │
│        IP address ↔ MAC address         │
└───────────────────┬─────────────────────┘
                    │
┌───────────────────▼─────────────────────┐
│                  TAP                    │
└─────────────────────────────────────────┘

-----------------------------------------------------------------------------------------------------
after a better cleanup for main.cpp and implementing a function ArpHandler,
the whole main.cpp becomes:

#include <iostream>
#include "net/tap.hpp"
#include "net/ethernet.hpp"
#include "net/arp.hpp"
#include "net/ipv4.hpp"
#include "net/icmpv4.hpp"

void ArpHandler(const EthernetFrame &ethernet, TapDevice &tap, std::uint32_t ourIp)
{
    ArpPacket arp =
        ArpPacket::parse(ethernet.payload());

    if (arp.opCode() != 1)
    {
        std::cout << "Not an ARP request. Ignoring.\n";
        return;
    }

    if (arp.targetIp() != ourIp)
    {
        std::cout << "ARP request is not for us. Ignoring.\n";
        return;
    }

    // ARP REPLY CONSTRUCTION
    MacAddress ourMac = {
        0x56, 0x1d, 0x27, 0x4d, 0xbf, 0x83};

    ArpPacket reply =
        ArpPacket::createReply(arp, ourMac, ourIp);

    auto replyPayLoad = reply.serialize();

    // MacAddress is std::array so i need to convert it into vector
    std::vector<unsigned char> destinationMac = {
        reply.targetMac()[0],
        reply.targetMac()[1],
        reply.targetMac()[2],
        reply.targetMac()[3],
        reply.targetMac()[4],
        reply.targetMac()[5]};

    std::vector<unsigned char> sourceMac = {
        reply.senderMac()[0],
        reply.senderMac()[1],
        reply.senderMac()[2],
        reply.senderMac()[3],
        reply.senderMac()[4],
        reply.senderMac()[5]};

    auto replyFrame = EthernetFrame::build(
        destinationMac,
        sourceMac,
        0x0806,
        replyPayLoad);

    tap.writeFrame(replyFrame);

    std::cout << "ARP Reply Sent!\n";
}

int main()
{
    TapDevice tap("tap0");

    std::uint32_t ourIp =
        (10u << 24) |
        (0u << 16) |
        (0u << 8) |
        1u;

    while (true)
    {
        auto raw_frame = tap.readFrame();

        EthernetFrame ethernet =
            EthernetFrame::parse(raw_frame);

        if (ethernet.etherType() == 0x0800)
        {
            std::cout << "Received an IPv4 frame\n";

            Ipv4Packet ipv4 =
                Ipv4Packet::parse(ethernet.payload());

            std::cout << "IPv4 packet received.\n";

            if (ipv4.protocol() == 1)
            {
                Icmpv4Packet icmp =
                    Icmpv4Packet::parse(ipv4.payload());

                std::cout << "ICMP Echo Request received.\n";

                if (icmp.type() == 8 && icmp.code() == 0)
                {
                    Icmpv4Packet replyIcmp =
                        Icmpv4Packet::createEchoReply(icmp);

                    auto replyIcmpPayload =
                        replyIcmp.serialize();

                    Ipv4Packet replyIpv4 =
                        Ipv4Packet::createReply(
                            ipv4,
                            replyIcmpPayload);

                    auto replyIpv4Payload =
                        replyIpv4.serialize();

                    std::cout << "ICMP Echo Reply created.\n";

                    auto replyFrame = EthernetFrame::build(
                        ethernet.sourceMac(), // because we are replying to the source of the request
                        ethernet.destinationMac(),
                        0x0800,
                        replyIpv4Payload);

                    tap.writeFrame(replyFrame);

                    std::cout << "ICMP Echo Reply sent.\n";
                }
            }

            continue;
        }

        if (ethernet.etherType() == 0x0806)
        {
            ArpHandler(ethernet, tap, ourIp);
            continue;
        }

        if (ethernet.etherType() == 0x86DD)
        {
            // ipv6 is not supported yet, so we will ignore it for now
            continue;
        }
        std::cout << "Unknown EtherType: 0x"
                  << std::hex
                  << ethernet.etherType()
                  << std::dec
                  << "\n";
    }
}

then to make the ipv4 handling cleaner i do the same for it:

void Ipv4Handler(EthernetFrame &ethernet, TapDevice &tap)
{
    Ipv4Packet ipv4 =
        Ipv4Packet::parse(ethernet.payload());

    std::cout << "IPv4 packet received.\n";

    if (ipv4.protocol() == 1)
    {
        Icmpv4Packet icmp =
            Icmpv4Packet::parse(ipv4.payload());

        std::cout << "ICMP Echo Request received.\n";

        if (icmp.type() == 8 && icmp.code() == 0)
        {
            Icmpv4Packet replyIcmp =
                Icmpv4Packet::createEchoReply(icmp);

            auto replyIcmpPayload =
                replyIcmp.serialize();

            Ipv4Packet replyIpv4 =
                Ipv4Packet::createReply(
                    ipv4,
                    replyIcmpPayload);

            auto replyIpv4Payload =
                replyIpv4.serialize();

            std::cout << "ICMP Echo Reply created.\n";

            auto replyFrame = EthernetFrame::build(
                ethernet.sourceMac(), // because we are replying to the source of the request
                ethernet.destinationMac(),
                0x0800,
                replyIpv4Payload);

            tap.writeFrame(replyFrame);

            std::cout << "ICMP Echo Reply sent.\n";
        }
    }
}
----------------------------------------------------------------------------------------------------
Ok right now we have these magic numbers:

0x0800   // IPv4
0x0806   // ARP
0x86DD   // IPv6
1        // ICMP
8        // Echo Request

we need to name them before starting with UDP, it will be much easier

after the includes we do:

constexpr std::uint16_t ETHERTYPE_IPV4 = 0x0800;
constexpr std::uint16_t ETHERTYPE_ARP  = 0x0806;
constexpr std::uint16_t ETHERTYPE_IPV6 = 0x86DD;

constexpr std::uint8_t IP_PROTOCOL_ICMP = 1;

constexpr std::uint8_t ICMP_ECHO_REQUEST = 8;
constexpr std::uint8_t ICMP_ECHO_REPLY   = 0;

so it will be:

#include <iostream>

#include "net/tap.hpp"
#include "net/ethernet.hpp"
#include "net/arp.hpp"
#include "net/ipv4.hpp"
#include "net/icmpv4.hpp"

constexpr std::uint16_t ETHERTYPE_IPV4 = 0x0800;
constexpr std::uint16_t ETHERTYPE_ARP  = 0x0806;
constexpr std::uint16_t ETHERTYPE_IPV6 = 0x86DD;

constexpr std::uint8_t IP_PROTOCOL_ICMP = 1;

constexpr std::uint8_t ICMP_ECHO_REQUEST = 8;
constexpr std::uint8_t ICMP_ECHO_REPLY   = 0;

using constexpr gives us a compile time advantage over normal const:

constexpr guarantees to the compiler that the value is fixed and known at compile time.

The compiler evaluates these constants during compilation and directly substitutes their values into
the generated machine code (e.g., immediate operands in assembly).

There is zero memory allocation or runtime lookup cost, matching the performance of raw magicnumbers.

-----------------------------------------------------------------------------------------------------
# My next milestone will be UDP.

roadmap:

Ethernet
   ↓
IPv4
   ↓
UDP
   ↓
Application data

UDP header is only 8 bytes (64 bits).

  0                15 16               31
 +-------------------+-------------------+
 |   Source Port     | Destination Port  |
 +-------------------+-------------------+
 |       Length      |     Checksum      |
 +-------------------+-------------------+
 |                                       |
 |              Payload                  |
 |                                       |
 +---------------------------------------+

so we need something similar to what we built previously:

- UdpPacket::parse()
- UdpPacket::serialize()
- source/destination ports
- length
- checksum
- UDP handling inside IPv4
- sending UDP packets
then eventually a simple application/socket abstraction --> we will build a simple app using my stack with UDP.

## First UDP step: create the UDP packet representation and parser.
we need udp.hpp and udp.cpp

Source Port       2 bytes
Destination Port  2 bytes
Length            2 bytes
Checksum          2 bytes  ← UDP header field

here we need a chekcsum field so we can use it to be calculated in the internetChecksum method.

One subtle point: UDP's checksum is not simply the checksum of the UDP header + payload. When we implement it, we'll need the IPv4 pseudo-header too:

IPv4 source IP
IPv4 destination IP
zero
protocol (17)
UDP length
UDP header
UDP payload

the udp.hpp will be:

#pragma once
#include <cstdint>
#include <vector>

class UdpPacket
{

public:
    static UdpPacket parse(const std::vector<unsigned char> &data);

    std::vector<unsigned char> serialize() const;

    std::uint16_t sourcePort() const;
    std::uint16_t destinationPort() const;
    std::uint16_t length() const;
    std::uint16_t checksum() const;
    std::vector<unsigned char> &payload() const;

private:
    std::uint16_t sourcePort_ = 0;
    std::uint16_t destinationPort_ = 0;
    std::uint16_t length_ = 0;
    std::uint16_t checksum_ = 0;
    std::vector<unsigned char> payload_;
};

## now we will implement parse in udp.cpp using big endian method.
lets remember :

Source Port       2 bytes
Destination Port  2 bytes
Length            2 bytes
Checksum          2 bytes

So:
bytes 0–1 → source port
bytes 2–3 → destination port
bytes 4–5 → UDP length
bytes 6–7 → checksum
bytes 8+ → payload

udp.cpp will be:

#include "net/udp.hpp"
#include <stdexcept>

UdpPacket UdpPacket::parse(const std::vector<unsigned char> &data)
{
    if (data.size() < 8)
    {
        throw std::runtime_error("UDP packet is too short");
    }
    UdpPacket packet;
    packet.sourcePort_ = (static_cast<std::uint16_t>(data[0]) << 8) |
                         (static_cast<std::uint16_t>(data[1]));

    packet.destinationPort_ = (static_cast<std::uint16_t>(data[2]) << 8) |
                              (static_cast<std::uint16_t>(data[3]));

    packet.length_ = (static_cast<std::uint16_t>(data[4]) << 8) |
                     (static_cast<std::uint16_t>(data[5]));

    packet.checksum_ = (static_cast<std::uint16_t>(data[6]) << 8) |
                       (static_cast<std::uint16_t>(data[7]));

    if (packet.length_ < 8)
    {
        throw std::runtime_error(
            "Invalid UDP length");
    }

    if (packet.length_ > data.size())
    {
        throw std::runtime_error(
            "UDP packet is truncated");
    }

    packet.payload_.assign(data.begin() + 8, data.begin() + packet.length_);

    return packet;
}

then we add getters outside parse:


std::uint16_t UdpPacket::sourcePort() const
{
    return sourcePort_;
}
std::uint16_t UdpPacket::destinationPort() const
{
    return destinationPort_;
}
std::uint16_t UdpPacket::length() const
{
    return length_;
}
std::uint16_t UdpPacket::checksum() const
{
    return checksum_;
}
const std::vector<unsigned char> &UdpPacket::payload() const
{
    return payload_;
}

-------------------------------------------------------------------------------------------------------
## now we will implement serialize(), as we know serialization process is the opposite of parse:

parse() : bytes → UdpPacket object

serialize() : UdpPacket object → bytes

we'll create a vector, reserve enough space for the 8-byte header + payload, then append each field in network byte order.

vector.reserve(n) allocates memory space for at least n elements ahead of time.

we use the network byte order (big-endian):

we put it under the getters in udp.cpp:

std::vector<unsigned char> UdpPacket::serialize() const
{
    std::vector<unsigned char> data;
    data.reserve(8 + payload_.size());

    // Source Port
    data.push_back(static_cast<unsigned char>(sourcePort_ >> 8));
    data.push_back(static_cast<unsigned char>(sourcePort_));

    // Destination Port
    data.push_back(static_cast<unsigned char>(destinationPort_ >> 8));
    data.push_back(static_cast<unsigned char>(destinationPort_));

    // Length
    data.push_back(static_cast<unsigned char>(length_ >> 8));
    data.push_back(static_cast<unsigned char>(length_));

    // Checksum
    data.push_back(static_cast<unsigned char>(checksum_ >> 8));
    data.push_back(static_cast<unsigned char>(checksum_));

    // Payload
    data.insert(data.end(), payload_.begin(), payload_.end());

    return data;
}
the last line data.insert(data.end(), payload_.begin(), payload_.end()); 
how does it work:

data.end() points at the end of the data vector

payload_.begin() and payload_.end() is a range of values that can be inserted after the end of data.

e.g.:

int main() {
    vector<int> v{1, 2, 3,};
    list<int> l{4, 5};

    // Inserting all elements of l to v
    v.insert(v.begin() + 3, l.begin(), l.end());
  
    for (auto i : v)
        cout << i << " ";
    return 0;
}
output will be: 1 2 3 4 5 

-----------------------------------------------------------------------------------------------------
# Next step: Testing the UDP to see if it works before moving to the checksum.

as we did before, we will do round-trip test

let's create udp_test.cpp 
and put this test for example inside it:

#include "net/udp.hpp"

#include <cassert>
#include <iostream>
#include <vector>

int main()
{
    std::vector<unsigned char> raw = {
        0x04, 0xD2, // source port: 1234
        0x16, 0x2E, // destination port: 5678
        0x00, 0x0D, // length: 13 (8 + 5)
        0x00, 0x00, // checksum
        'h', 'e', 'l', 'l', 'o'};

        // 'h', 'e', 'l', 'l', 'o' here is the payload

    UdpPacket packet =
        UdpPacket::parse(raw);

    assert(packet.sourcePort() == 1234);
    assert(packet.destinationPort() == 5678);
    assert(packet.length() == 13);
    assert(packet.checksum() == 0);

    const auto &payload = packet.payload();

    assert(payload.size() == 5);
    assert(payload[0] == 'h');
    assert(payload[1] == 'e');
    assert(payload[2] == 'l');
    assert(payload[3] == 'l');
    assert(payload[4] == 'o');

    auto serialized = packet.serialize();

    assert(serialized == raw);

    std::cout << "UDP round-trip test passed!\n";

    return 0;
}

our raw packet is:
04 D2       → 1234
16 2E       → 5678
00 0D       → 13 bytes
00 00       → checksum
68 65 6C 6C 6F → "hello"

assert is a preprocessor macro used to test assumptions and catch logic errors during development. It evaluates a given conditional expression at runtime.

if true (non-zero) -> The program does nothing and continues executing normally

if false (zero) -> The program immediately prints a diagnostic error message to stderr (including the source file name, line number, and the expression that failed) and aborts execution via std::abort().

e.g.: lets say i wanted to process an age:

i need #include<cassert>
so:

#include <iostream>
#include <cassert> // Required header

void processAge(int age){

    assert(age > 0);

    std::cout<<"processing age "<< age<< "/n";
}

int main(){
    processAge(-1);
}

here its less than -1 so it will give me an error like: main.o: /tmp/YkfA8cQdAr-wfxYqx0j/main.cpp:6: void processAge(int): Assertion `age > 0' failed.
Aborted

## Now remember:
UDP header = 8 bytes
payload    = 5 bytes
--------------------
total      = 13 bytes

UdpPacket::parse(raw) turns those bytes into our UdpPacket object 

packet.serialize() turns this object into bytes

then in cmake we add it as an independent executable


## Next Step: calculate the UDP length automatically.

right now we have std::uint16_t length_ = 0; but there's no clean way to create a new UdpPacket with a payload and have length_ automatically become 8 + payload_.size()

### step 1 to calculate it: Adding a constructor.
in udp.hpp we add:

UdpPacket(std::uint16_t sourcePort, std::uint16_t destinationPort,
              const std::vector<unsigned char> &payload);

Notice we added source port, destination port and payload

then we will implement it inside udp.cpp.

so the constructor will say:

source port      = provided value
destination port = provided value
payload          = provided data
length           = 8 + payload size
checksum         = 0 for now

we add this at the top of udp.cpp:


UdpPacket::UdpPacket(std::uint16_t sourcePort, std::uint16_t destinationPort,
                     const std::vector<unsigned char> &payload)
    : sourcePort_(sourcePort), destinationPort_(destinationPort),
      length_(8 + payload.size()), checksum_(0), payload_(payload)
{
}

these ->  : sourcePort_(sourcePort), destinationPort_(destinationPort),
      length_(8 + payload.size()), checksum_(0), payload_(payload)
      
      will take the private variables and says: "when you return a value to the constructor give it an initializer.

simple example:

#include <iostream>
#include <string>

class Player {
private:
    std::string name_;
    int score_;

public:
    // Constructor using a member initializer list
    Player(const std::string& name, int score)
        : name_(name), score_(score) // Initializing member variables here
    {
        // The constructor body can be completely empty!
    }

    void printInfo() const {
        std::cout << "Player: " << name_ << " | Score: " << score_ << std::endl;
    }
};

int main() {
    // Creates a Player object passing "Alice" and 100 to the constructor
    Player player1("Alice", 100);
    player1.printInfo(); // Output: Player: Alice | Score: 100

    return 0;
}

this will take whats in private and initialize it with the given values that returned to the constructor.
Player(const std::string& name, int score)
        : name_(name), score_(score) 

its like i intialized them inside the constructor. both are valid.

it is like this :  constructor (para1, para2) : privateMember1(para1), privateMember2(para2){}

the body can be empty.

then we will re-construct the udp.cpp to be like this:

#include "net/udp.hpp"
#include <stdexcept>

UdpPacket::UdpPacket(std::uint16_t sourcePort, std::uint16_t destinationPort,
                     const std::vector<unsigned char> &payload)
    : sourcePort_(sourcePort), destinationPort_(destinationPort),
      length_(8 + payload.size()), checksum_(0), payload_(payload)
{
}

UdpPacket UdpPacket::parse(const std::vector<unsigned char> &data)
{
    if (data.size() < 8)
    {
        throw std::runtime_error("UDP packet is too short");
    }

    std::uint16_t sourcePort = (static_cast<std::uint16_t>(data[0]) << 8) |
                               (static_cast<std::uint16_t>(data[1]));

    std::uint16_t destinationPort = (static_cast<std::uint16_t>(data[2]) << 8) |
                                    (static_cast<std::uint16_t>(data[3]));

    std::uint16_t length = (static_cast<std::uint16_t>(data[4]) << 8) |
                           (static_cast<std::uint16_t>(data[5]));

    std::uint16_t checksum = (static_cast<std::uint16_t>(data[6]) << 8) |
                             (static_cast<std::uint16_t>(data[7]));

    if (length < 8)
    {
        throw std::runtime_error("Invalid UDP length");
    }

    if (length > data.size())
    {
        throw std::runtime_error("UDP packet is truncated");
    }

    std::vector<unsigned char> payload(
        data.begin() + 8, data.begin() + length);

    UdpPacket packet(sourcePort, destinationPort, payload);

    packet.checksum_ = checksum;

    return packet;
}

std::uint16_t UdpPacket::sourcePort() const
{
    return sourcePort_;
}
std::uint16_t UdpPacket::destinationPort() const
{
    return destinationPort_;
}
std::uint16_t UdpPacket::length() const
{
    return length_;
}
std::uint16_t UdpPacket::checksum() const
{
    return checksum_;
}
const std::vector<unsigned char> &UdpPacket::payload() const
{
    return payload_;
}

std::vector<unsigned char> UdpPacket::serialize() const
{
    std::vector<unsigned char> data;
    data.reserve(8 + payload_.size());

    // Source Port
    data.push_back(static_cast<unsigned char>(sourcePort_ >> 8));
    data.push_back(static_cast<unsigned char>(sourcePort_));

    // Destination Port
    data.push_back(static_cast<unsigned char>(destinationPort_ >> 8));
    data.push_back(static_cast<unsigned char>(destinationPort_));

    // Length
    data.push_back(static_cast<unsigned char>(length_ >> 8));
    data.push_back(static_cast<unsigned char>(length_));

    // Checksum
    data.push_back(static_cast<unsigned char>(checksum_ >> 8));
    data.push_back(static_cast<unsigned char>(checksum_));

    // Payload
    data.insert(data.end(), payload_.begin(), payload_.end());

    return data;
}

now we have a nice design:

Creating a new packet
        ↓
UdpPacket(S-port, D-port, payload)
        ↓
length = 8 + payload.size()


Receiving a packet
        ↓
UdpPacket::parse(bytes)
        ↓
read length from network
        ↓
construct UdpPacket
        ↓
preserve received checksum

### step 2: testing the constructor: we will return to udp_test.cpp and do these changes:

#include "net/udp.hpp"

#include <cassert>
#include <iostream>
#include <vector>

int main()
{
    std::vector<unsigned char> payload = {
        'h', 'e', 'l', 'l', 'o'};

    UdpPacket packet(
        1234,
        5678,
        payload);

    assert(packet.sourcePort() == 1234);
    assert(packet.destinationPort() == 5678);
    assert(packet.length() == 13);
    assert(packet.checksum() == 0);
    assert(packet.payload() == payload);

    std::vector<unsigned char> raw = {
        0x04, 0xD2, // source port: 1234
        0x16, 0x2E, // destination port: 5678
        0x00, 0x0D, // length: 13 (8 + 5)
        0x00, 0x00, // checksum
        'h', 'e', 'l', 'l', 'o'};

    UdpPacket parsedPacket =
        UdpPacket::parse(raw);

    assert(parsedPacket.sourcePort() == 1234);
    assert(parsedPacket.destinationPort() == 5678);
    assert(parsedPacket.length() == 13);
    assert(parsedPacket.checksum() == 0);

    const auto &parsedPayload = parsedPacket.payload();

    assert(parsedPayload.size() == 5);
    assert(parsedPayload[0] == 'h');
    assert(parsedPayload[1] == 'e');
    assert(parsedPayload[2] == 'l');
    assert(parsedPayload[3] == 'l');
    assert(parsedPayload[4] == 'o');

    auto serialized = parsedPacket.serialize();

    assert(serialized == raw);

    std::cout << "UDP round-trip test passed!\n";

    return 0;
}

we did two tests: one for the packet and one for the raw parsed packet.
-------------------------------------------------------------------------------------------------------
# Next: UDP checksum

Now we're getting into an important networking concept.

Unlike our ICMP checksum, UDP's checksum is calculated over:

IPv4 pseudo-header
        +
UDP header
        +
UDP payload

### First we need to understand/build the pseudo header of the UDP that contains:

0                   15 16                  31
+---------------------+---------------------+
|                 Source IP                |
+-------------------------------------------+
|              Destination IP              |
+----+--------+-----------------------------+
|  0 |   17   |         UDP Length          |
+----+--------+-----------------------------+

4 bytes  source IP
4 bytes  destination IP
1 byte   zero
1 byte   protocol = 17
2 bytes  UDP length

first we build the pseudo header in the udp.hpp in private:

std::vector<unsigned char> buildPseudoHeader(
        std::uint32_t sourceIp,
        std::uint32_t destinationIp,
        std::uint16_t udpLength);

then we implement it in udp.cpp:

we will need to reserve 12 in a vector because 4 + 4 + 1 + 1 + 2

std::vector<unsigned char> UdpPacket::buildPseudoHeader(std::uint32_t sourceIp, std::uint32_t destinationIp,
                                       std::uint16_t udpLength)
{
    std::vector<unsigned char> pseudoHeader;
    pseudoHeader.reserve(12);

    // Source IP
    pseudoHeader.push_back(static_cast<unsigned char>(sourceIp >> 24));
    pseudoHeader.push_back(static_cast<unsigned char>(sourceIp >> 16));
    pseudoHeader.push_back(static_cast<unsigned char>(sourceIp >> 8));
    pseudoHeader.push_back(static_cast<unsigned char>(sourceIp));

    // Destination IP
    pseudoHeader.push_back(static_cast<unsigned char>(destinationIp >> 24));
    pseudoHeader.push_back(static_cast<unsigned char>(destinationIp >> 16));
    pseudoHeader.push_back(static_cast<unsigned char>(destinationIp >> 8));
    pseudoHeader.push_back(static_cast<unsigned char>(destinationIp));

    // Zero
    pseudoHeader.push_back(0);

    // Protocol (UDP is 17)
    pseudoHeader.push_back(17);

    // UDP Length
    pseudoHeader.push_back(static_cast<unsigned char>(udpLength >> 8));
    pseudoHeader.push_back(static_cast<unsigned char>(udpLength));

    return pseudoHeader;
}

okay now we calculate the checksum so we need in udp.hpp:

 std::uint16_t calcChecksum(std::uint32_t sourceIp, std::uint32_t destinationIp) const;

the sender takes the IP addresses and split them into 16-bits chunks and add them using 1's complement sum

(Note: in every checksum, the sender side: checksum creation, the receiver side: checksum validation)

example:

• Source IP: 192.168.1.10
• Destination IP: 10.0.0.5

Convert to Hexadecimal and Split into 16-bit Blocks: Each IP address is 4 bytes (32 bits), so we convert them to hex and split them right down the middle into two 16-bit (2-byte) blocks:

• Source IP (192.168.1.10):
	• 192.168 in hex is C0A8
	• 1.10 in hex is 010A

• Destination IP (10.0.0.5):
	• 10.0 in hex is 0A00
	• 0.5 in hex is 0005
	• Blocks: 0A00 and 0005

Step-by-Step 1's Complement Addition:
add the two source ip blocks:

  C0A8  (1100000010101000)
+ 010A  (0000000100001010)
------
  C1B2  (1100000110110010)  <- No carry bit, proceed normally

we add the first destination ip block to that C1B2:

  C1B2  (1100000110110010)
+ 0A00  (0000101000000000)
------
  CBB2  (1100101110110010)  <- No carry bit, proceed normally

then we add the second destination ip block:

  CBB2  (1100101110110010)
+ 0005  (0000000000000101)
------
  CBB7  (1100101110110111)  <- Sum of all blocks

The Final step (1's Complement)

To get the checksum to invert every single bit (change 0s to 1s, and 1s to 0s).

• Sum in Binary: 1100 1011 1011 0111 (CBB7)
• Inverted Binary: 0011 0100 0100 1000
• Final 16-bit Checksum: 3448

it will be:

std::uint16_t UdpPacket::calcChecksum(std::uint32_t sourceIp, std::uint32_t destinationIp) const
{
    std::vector<unsigned char> pseudoHeader = buildPseudoHeader(sourceIp, destinationIp, length_);
    std::vector<unsigned char> udpData;
    udpData.reserve(length_);

    // Source Port
    udpData.push_back(static_cast<unsigned char>(sourcePort_ >> 8));
    udpData.push_back(static_cast<unsigned char>(sourcePort_));

    // Destination Port
    udpData.push_back(static_cast<unsigned char>(destinationPort_ >> 8));
    udpData.push_back(static_cast<unsigned char>(destinationPort_));

    // Length
    udpData.push_back(static_cast<unsigned char>(length_ >> 8));
    udpData.push_back(static_cast<unsigned char>(length_));

    // Checksum (set to 0 for calculation)
    udpData.push_back(0);
    udpData.push_back(0);

    // Payload
    udpData.insert(udpData.end(), payload_.begin(), payload_.end());

    // Combine pseudo-header and UDP data
    pseudoHeader.insert(pseudoHeader.end(), udpData.begin(), udpData.end());
    
    std::uint16_t checksum = internetChecksum(pseudoHeader);

    // if udp checksum is 0 there is no checksum, so we it will return 0xFFFF
    if (checksum == 0)
    {
        checksum = 0xFFFF;
    }

    return checksum;
}

in here: udpData.push_back(0);
udpData.push_back(0); we must not use checksum_ because the checksum calculation needs the checksum field itself to be zero.

revision:

when we want to unpack raw bytes we convert from unsigned char to uint16 or uint32 depending on size of the integer we want to have. the value will be displayed in decimal. e.g:

#include<iostream>
#include<cstdint>

int main(){
    unsigned char buffer[4] = {0x00, 0x00, 0x01, 0x02}; // Raw bytes from a file

   // Reconstructing a 32-bit integer from the bytes
    uint32_t value = ((uint32_t)buffer[0] << 24) |
                 ((uint32_t)buffer[1] << 16) |
                 ((uint32_t)buffer[2] << 8)  |
                  (uint32_t)buffer[3];
    std::cout<<value;
    
}

the value will be 258:

• (0x00 << 24) \(\rightarrow \) 0x00000000
• (0x00 << 16) \(\rightarrow \) 0x00000000
• (0x01 << 8)  \(\rightarrow \) 0x00000100 (which is 256 in decimal)
• (0x02)       \(\rightarrow \) 0x00000002 (which is 2 in decimal)

packing is the opposite of this process.

-------------------------------------------------------------------------------------------------------
# Next Step: Checksum Verification.

Now we need to be able to take a received UDP packet and determine whether its checksum is valid.

## What is it?
UDP checksum verification is a lightweight error-detection mechanism used to check if a User Datagram Protocol (UDP) packet has been corrupted during transmission. It is mandatory in IPv6 and optional (set to all zeros if unused) in IPv4.

It is essentially the exact mathematical inverse of the calculation. (like parse and serialization)

Sender side: calculation:
The sender calculates A + B = C, and then sends you A, B, and the negative version (one's complement) of C, which we can write as -C.

Receiver side: verification:
The receiver adds them all together: A + B + (-C).
If the data hasn't changed, A + B still equals C, so C + (-C) = 0.

Note: the verification is the same size as the calculation (16 bits)

our logic should be:

Received packet
      ↓
Is checksum == 0?
      ↓
   yes → valid/no-checksum
      ↓
   no
      ↓
pseudo-header
      +
entire UDP packet INCLUDING received checksum
      ↓
internetChecksum()
      ↓
result == 0x0000 ?
      ↓
 valid / invalid

The source port, destination port, length, and payload are already included in serialize(). The pseudo-header adds the source IP, destination IP, protocol number, and UDP length.

so our udp.hpp verifyChecksum() is: 

bool verifyChecksum(std::uint32_t sourceIp, std::uint32_t destinationIp) const;

because we only want true or false

in udp.cpp: 

bool UdpPacket::verifyChecksum(std::uint32_t sourceIp, std::uint32_t destinationIp) const
{
    // IPv4 allows a checksum of 0 to mean that no UDP checksum was provided.
    if (checksum_ == 0)
    {
        return true;
    }
    
    auto pseudoHeader = buildPseudoHeader(sourceIp, destinationIp, length_);
    auto udpData = serialize(); 

    pseudoHeader.insert(pseudoHeader.end(), udpData.begin(), udpData.end());
    
    return internetChecksum(pseudoHeader) == 0; 
}

(note: auto works like var in javascript or GDscript)

-------------------------------------------------------------------------------------------------------
# making checksum_ actually get populated for outgoing UDP packets.

in udp.hpp we do:

void setChecksum(std::uint32_t sourceIp, std::uint32_t destinationIp);

and in udp.cpp:

void UdpPacket::setChecksum(std::uint32_t sourceIp, std::uint32_t destinationIp)
{
    checksum_ = calcChecksum(sourceIp, destinationIp);
}

we basically did:

calcChecksum()
    → calculate and return

setChecksum()
    → calculate and store

verifyChecksum()
    → check received checksum

-------------------------------------------------------------------------------------------------------
# Now in main.cpp:
 i need #include "udp.hpp"

then we put after the ICMP echo if statement :

else if (ipv4.protocol() == IP_PROTOCOL_UDP)
{
 UdpPacket udp = UdpPacket::parse(ipv4.payload());
 std::cout << "UDP Packet Received";

 if (!udp.verifyChecksum(ipv4.sourceIp(), ipv4.destinationIp()))
 {
    std::cout << "Invalid UDP Checksum";
     return;
 }

std::cout << "UDP Checksum Verified";

std::cout << "Source Port: " << udp.sourcePort() << "\n";
std::cout << "Destination Port: " << udp.destinationPort() << "\n";
std::cout << "Payload Size: " << udp.payload().size() << "\n";
}

we basically checked if the protocol that returned is a UDP packet.
and to see if the checksum is valid or not.
if it was valid then print source port, destination port and payload size

-------------------------------------------------------------------------------------------------------
# Now before UDP sending we need some things:
 our order should be:

1- ARP cache — store mappings between IPv4 addresses and MAC addresses.

2- ARP request generation — ask for the MAC address when it's unknown.

3- ARP reply handling — learn the destination's MAC address and update the cache.

4- UDP sending — use the resolved MAC address to send UDP packets through IPv4 and Ethernet.

overall frame:

Send UDP packet
      ↓
Look up destination IP in ARP cache
      ↓
  MAC found?
   ↙     ↘
 Yes      No
  ↓        ↓
Send    ARP request
          ↓
      Receive reply
          ↓
      Update cache
          ↓
      Send UDP packet

## First: ARP cache:

Creating ArpCache: We'll store mappings between an IPv4 address and its MAC address. For example:
10.0.0.2 → 56:1d:27:4d:bf:83
10.0.0.3 → aa:bb:cc:dd:ee:ff

I'll use std::unordered_map for quick lookups. Since my project already uses a MacAddress type, I'll reuse it.

lesson:

unordered_map: organizes elements into buckets based on their hash value. This design enables an average-case time complexity of O(1) for insertion, deletion, and lookups.

so unordered map is built on hashing, the keys should be unique.
its hash table is internal, meaning operations like insertion, lookup, and deletion are incredibly fast 

a complete example on how unordered maps work:

// Online C++ compiler (editor)
// Write and run C++ online using this editor.

#include <iostream>
#include <string>
#include <unordered_map>
int main() {
    //initialization example
    std::unordered_map<std::string, int>Map = {
        {"apple", 12},
        {"orange", 14},
        {"banana", 20}
    };

    // insertion:

    //first type is []
    Map["juice"] = 20;
   

    //second type is insert() function
    Map.insert({"mango",44});

    // 3rd type: changing an existing value through its key:
    Map["apple"] = 99;

    //lookup (searching for a key if exists):

    std::string search_key = "banana";
    if (Map.find(search_key) != Map.end())
    { // will move through the map until it find or not find it

        std::cout << search_key << " is in stock. Quantity: " << Map[search_key] << "\n\n";
    }
    else{
        std::cout << search_key << " is out of stock.\n\n";
    }

    //erasing element:
    Map.erase("orange");

    //iterating through the map
    // Note: The output order will likely not match the insertion order
    std::cout << "Current Inventory:\n";
    for (const auto& pair : Map) 
    {
        // pair.first contains the key, pair.second contains the value
        std::cout << "Item: " << pair.first << " | Quantity: " << pair.second << "\n";
    }
     return 0;
}

the output will be: 

banana is in stock. Quantity: 20

Current Inventory:
Item: mango | Quantity: 44
Item: juice | Quantity: 20
Item: banana | Quantity: 20
Item: apple | Quantity: 99


### Returning to our arp_cache.hpp
we will have:

#pragma once

#include "net/arp.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>

class ArpCache
{
public:
    void add(std::uint32_t ip, const MacAddress &mac);

    std::optional<MacAddress> lookup(std::uint32_t ip) const;

private:
    std::unordered_map<std::uint32_t, MacAddress> entries_;
    // the IP will be the key, and the MAC address will be the value
};

now we make arp_cache.cpp to implement this add function:

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

it -> second means return the key's value and not the key itself 
since the unordered map is <key_here, value_here>

notice we used ->second instead of .second because the mac is returned by reference and not by value.
so we access it using a pointer

after this we add the arp_cache.cpp to the cmake file executable of tcp_stack and not the test executable.

### Now we'll connect the ARP cache to my running stack in main.cpp:

at the moment our ArpHandler receives Ethernet frame, TAP device and an IP address.
so we will add the ArpCache& parameter so the handler can remember which MAC address belongs to which IP.

so we will have:
void ArpHandler(const EthernetFrame &ethernet,
                 TapDevice &tap, std::uint32_t ourIp,
                 ArpCache &arpCache)

then before checking the op code and after parsing: 

if (arp.senderIp() != 0){
        arpCache.add(arp.senderIp(), arp.senderMac());
        // we add them because we want to cache the sender's IP and MAC address for future use.
}

we check if it was zero or not. because if it was zero, we can't cache it because there is no IP yet.

then inside main before the while loop:

    ArpCache arpCache;

then in the ArpHandler calling we do:

ArpHandler(ethernet, tap, ourIp, arpCache);

instead of ArpHandler(ethernet, tap, ourIp);

-------------------------------------------------------------------------------------------------------
# building an ARP request.

in arp.hpp we add :

 static ArpPacket createRequest(
        const MacAddress &senderMac,
        std::uint32_t senderIp,
        std::uint32_t targetIp);

senderMac: our interface's MAC address.
senderIp: our stack's IP address.
targetIp: the IP address whose MAC we want to discover.

then we implement it in arp.cpp:

ArpPacket ArpPacket::createRequest(const MacAddress &senderMac, std::uint32_t senderIp,
                                   std::uint32_t targetIp)
{
    ArpPacket request;

    request.hardware_type_ = 1; // Ethernet
    request.protocol_type_ = 0x0800; // IPv4

    request.hardware_size_ = 6; // MAC address size
    request.protocol_size_ = 4; // IPv4 address size

    request.op_code_ = 1; // ARP Request

    request.sender_mac_ = senderMac;
    request.sender_ip_ = senderIp;

    request.target_mac_ = {0, 0, 0, 0, 0, 0}; // Unknown target MAC
    request.target_ip_ = targetIp;

    return request;
}

An Ethernet/IPv4 ARP request containing:
- Our MAC and IP as the sender.
- The destination IP we want to resolve.
- A zero-filled target MAC, because we don't know it yet.
- Opcode 1, identifying it as a request.

------------------------------------------------------------------------------------------------------
# Add a function to send an ARP request:

in main.cpp we add this function near ArpHandler:

void SendArpRequest(
    TapDevice &tap,
    const MacAddress &ourMac,
    std::uint32_t ourIp,
    std::uint32_t targetIp)
{
    MacAddress broadcastMac = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    }; // because MAC is 6 groups of 2 Hex digits

    ArpPacket request =
        ArpPacket::createRequest(ourMac, ourIp, targetIp);

    auto arpPayload = request.serialize();

    std::vector<unsigned char> destinationMac(
        broadcastMac.begin(), broadcastMac.end());

    std::vector<unsigned char> sourceMac(
        ourMac.begin(), ourMac.end());

    auto frame = EthernetFrame::build(
        destinationMac,
        sourceMac,
        ETHERTYPE_ARP,
        arpPayload);

    tap.writeFrame(frame);

    std::cout << "ARP Request Sent!\n";
}

What this does
1. Creates the broadcast MAC address FF:FF:FF:FF:FF:FF.
2. Builds an ARP request for the target IP.
3. Serializes the request into bytes.
4. Wraps those bytes in an Ethernet frame with EtherType 0x0806.
5. Writes the frame to tap0.


## now we will modify this function so it can get connected to the ARP cache so the stack can request a MAC address only when it doesn’t already know one.

### Add a lookup-before-request function:

The logic we want is simple:
1. Check whether the target IP is in ArpCache.
2. If it is, return the MAC address.
3. If it isn’t, send an ARP request and report that the address is not known yet.

For now, we won’t wait for the reply; we’ll add asynchronous resolution later.

it becomes like this:

std::optional<MacAddress> ResolveMAC(ArpCache &arpCache, TapDevice &tap,
                                     const MacAddress &ourMac,
                                     std::uint32_t ourIp,
                                     std::uint32_t targetIp)
{
    auto cached_Mac = arpCache.lookup(targetIp);

    if (cached_Mac.has_value())
    {
        return cached_Mac;
    }
    SendArpRequest(tap, ourMac, ourIp, targetIp);

    return std::nullopt;
}

Why std::optional? The function may return a MAC address if it’s cached, or std::nullopt if it needs to ask the network.

note: sending the ARP request doesn't mean resolution has succeeded. The reply will arrive later, and my existing ArpHandler() can learn the sender's mapping.

Two things still needs attention:

1. My ArpHandler ignores ARP replies after caching them.

2. ResolveMAC() isn't called anywhere yet.

so in ArpHandler we will change:

if (arp.opCode() != 1)
{
    std::cout << "Not an ARP request. Ignoring.\n";
    return;
}

to: 
if (arp.opCode() == 2)
{
    std::cout << "ARP Reply Received; sender mapping cached.\n";
    return;
}

if (arp.opCode() != 1)
{
    std::cout << "Unknown ARP operation. Ignoring.\n";
    return;
}

## Next step — Make the ARP cache actually useful

then in main under TapDevice tap("tap0");
we put:

std::uint32_t ourIp =
        (10u << 24) |
        (0u << 16) |
        (0u << 8) |
        1u;
        
 MacAddress ourMac = {
        0x56, 0x1d, 0x27, 0x4d, 0xbf, 0x83}; // we added this after ourIp

## Next step — Pass ourMac into ArpHandler()

we update ArpHandler to have :

void ArpHandler(
    const EthernetFrame &ethernet,
    TapDevice &tap,
    std::uint32_t ourIp,
    const MacAddress &ourMac,
    ArpCache &arpCache)

then we remove the local Mac address decleration inside the ArpHandler: 

MacAddress ourMac = {
    0x56, 0x1d, 0x27, 0x4d, 0xbf, 0x83
}; // not the one in main

inside main when we call the arp handler we make it include ourMac:

ArpHandler(ethernet, tap, ourIp, ourMac, arpCache);

Why? Now both ARP requests and replies can use the same interface MAC address, configured once in main().

## Next step — Prevent repeated ARP requests
we have a problem in ResolveMac() and it is repeated requests.
ResolveMAC() is called multiple times for an IP address that isn't cached, it sends a new ARP request every time. We'll add a simple pending-resolution set so we only send one request while waiting for a reply.

## Step 1: Adding a pending set:

we include this in our main.cpp: #include <unordered_set>

then we reference it in the ResolveMac() by adding a parameter: 
std::unordered_set<std::uint32_t> &pendingRequests

and then checking if there was a value inserted in the set like unordered map
so our update will be like this:

std::optional<MacAddress> ResolveMAC(ArpCache &arpCache,
                                     TapDevice &tap,
                                     const MacAddress &ourMac,
                                     std::uint32_t ourIp,
                                     std::uint32_t targetIp,
                                     std::unordered_set<std::uint32_t> &pendingRequests)
{
    auto cached_Mac = arpCache.lookup(targetIp);

    if (cached_Mac.has_value())
    {
        return cached_Mac;
    }

    if (pendingRequests.insert(targetIp).second) // if there was a value inserted send a request
    {
        SendArpRequest(tap, ourMac, ourIp, targetIp);
    }

    return std::nullopt;
}
note: (sends a request if the IP wasn't already there in the set)

## Now: Pass the pending set to ArpHandler:
alongside ArpCache arpCache; we put: std::unordered_set<std::uint32_t> pendingArp;
then in ArpHandler we put in the parameters:

std::unordered_set<std::uint32_t> &pendingArp; //because i want to modify the value i receive from main itself that's why i use by reference.

then in the opcode check we do:

if (arp.opCode() == 2)
    {
        pendingArp.erase(arp.senderIp());
        std::cout << "ARP Reply Received; sender mapping cached.\n";
        return;

}
removes the IP address that just replied from the pending set. to not cause bloating and many requests

## Next step: Decide what happens to an outgoing packet when ResolveMAC() returns std::nullopt.

The ARP request goes out, but the original packet cannot be sent until the MAC address is learned. We’ll handle that with a small pending-packet queue before moving on to outgoing UDP.

our ResolveMac() works like this:
                  +-----------------------------------+
                  |       ResolveMAC(targetIp)        |
                  +-----------------------------------+
                                    |
                                    v
                  +-----------------------------------+
                  |    arpCache.lookup(targetIp)      |
                  +-----------------------------------+
                                    |
                                    v
                         { Is MAC in Cache? }
                        /                \
                (YES)  /                  \  (NO)
                      v                    v
        +-----------------------+  +-------------------------------+
        |      CACHE HIT        |  |          CACHE MISS           |
        | Return cached MAC     |  | Try insert targetIp into      |
        +-----------------------+  | pendingRequests set           |
                                   +-------------------------------+
                                                  |
                                                  v
                                     { Was insertion successful? }
                                    /                             \
                            (YES)  /                               \  (NO - Already Exists)
                                  v                                 v
                  +-------------------------------+       +-------------------------------+
                  | Send ARP Request via Tap      |       | Skip sending ARP request      |
                  | (First time seeing this IP)   |       | (Prevents duplicate flooding) |
                  +-------------------------------+       +-------------------------------+
                                  |                                       |
                                  +-------------------+-------------------+
                                                      |
                                                      v
                                      +-------------------------------+
                                      |     Return std::nullopt       |
                                      +-------------------------------+
from here we will decide what happens to the nullopt.

what are we trying to do? lets say my stack wants to send a UDP packet to 10.0.0.2.
It knows the destination IP, but Ethernet needs the destination's MAC address to send the frame.

so we will create a sending function in the main.cpp:

void SendIpv4Packet(TapDevice &tap,
                    const MacAddress &ourMac,
                    const MacAddress &destinationMac,
                    const std::vector<unsigned char> &ipv4Payload)
{
    std::vector<unsigned char> destination(destinationMac.begin(), destinationMac.end());
    std::vector<unsigned char> source(ourMac.begin(), ourMac.end());

    auto frame = EthernetFrame::build(
        destination,
        source,
        ETHERTYPE_IPV4,
        ipv4Payload);

    tap.writeFrame(frame);
}

what does this function do? It takes an already-known destination MAC address and an already-serialized IPv4 packet, wraps the packet in an Ethernet frame, and sends it through tap0.

## Next step: connect ResolveMAC() to SendIpv4Packet():

Before we add the pending-packet queue, we'll create a small helper that combines the two functions:
1. Ask ResolveMAC() for the destination MAC.
2. If it returns a MAC, send the IPv4 packet.
3. If it returns std::nullopt, report that resolution is pending.

so the helper will be:

void SendIpv4PacketWithARP(TapDevice &tap,
                           ArpCache &arpCache,
                           const MacAddress &ourMac,
                           std::uint32_t ourIp,
                           std::uint32_t targetIp,
                           const std::vector<unsigned char> &ipv4Payload,
                           std::unordered_set<std::uint32_t> &pendingRequests)
{
    auto destinationMac = ResolveMAC(arpCache, tap, ourMac, ourIp, targetIp, pendingRequests);

    if (destinationMac.has_value())
    {
        SendIpv4Packet(tap, ourMac, *destinationMac, ipv4Payload);
    }
    else
    {
        std::cout << "ARP resolution pending for destination IP.\n";
    }
}

## Now we need to fix the gap: when ARP resolution is pending, the IPv4 packet currently gets discarded. as we see in the else statement.

how can we fix it?

first:
let's add a pending-packet structure in the top of main.cpp before ArpHandler:

struct PendingPacket{
    std::uint32_t targetIp;
    std::vector<unsigned char> ipv4Payload;
};

- targetIp is the destination IP whose MAC address we want.

+-----------------------------+                               +-----------------------------+
|        Sender Node          |                               |        Target Node          |
|         (Our IP)            |                               |         (Target IP)         |
|       192.168.1.50          | -------- [ Packet ] --------> |          10.0.0.1           |
+-----------------------------+                               +-----------------------------+

we want the MAC for the second one.

- ipv4Payload: the serialized IPv4 packet we want to send later.

## Next step: we'll create a std::vector<PendingPacket> in main():
to hold packets waiting for ARP resolution. Then we'll update SendIpv4PacketTo() to queue a packet when ResolveMAC() returns std::nullopt, rather than losing it.

under std::unordered_set<std::uint32_t> pendingArp; in main() we put:

std::vector<PendingPacket> pendingPackets; -> this can reach to targetIp and ipv4Payload.

difference from arp:
- pendingArp: tracks which destination IPs are awaiting ARP replies, preventing duplicate requests.

- pendingPackets: stores the actual IPv4 packets that need to be sent once those replies arrive.

## Now we will do some changes in main.cpp:
1. queue packets and send them after ARP replies, we need to update SendIpv4PacketWithARP() function:

void SendIpv4PacketWithARP(TapDevice &tap,
                           ArpCache &arpCache,
                           const MacAddress &ourMac,
                           std::uint32_t ourIp,
                           std::uint32_t targetIp,
                           const std::vector<unsigned char> &ipv4Payload,
                           std::unordered_set<std::uint32_t> &pendingRequests,
                           std::vector<PendingPacket> &pendingPackets)
{
    auto destinationMac = ResolveMAC(arpCache, tap, ourMac, ourIp, targetIp, pendingRequests);

    if (destinationMac.has_value())
    {
        SendIpv4Packet(tap, ourMac, *destinationMac, ipv4Payload);
    }
    else
    {
        pendingPackets.push_back({targetIp, ipv4Payload});
        std::cout << "IPv4 packet queued; waiting for ARP reply.\n";
    }
}

what we did is simply queueing the targetIp and payload as a set in the pending packet indicating that it still waits for an ARP reply to arrive.

2. then i need a function to flush those queued packets compared to cache:

void FlushPendingPackets(TapDevice &tap,
                         ArpCache &arpCache,
                         const MacAddress ourMac,
                         std::vector<PendingPacket> &pendingPackets)
{

    auto it = pendingPackets.begin();
    while (it != pendingPackets.end())
    {
        auto destinationMac = arpCache.lookup(it->targetIp);
        if (destinationMac.has_value())
        {
            SendIpv4Packet(tap, ourMac, *destinationMac, it->ipv4Payload);
            it = pendingPackets.erase(it); // Remove the sent packet from the queue
        }
        else
        {
            ++it; // Move to the next packet if MAC address is still not resolved
        }
    }
}

This checks queued packets against the cache. When a MAC address is available, it sends the packet and removes it from the queue. (simply: it flushes the pending packet that have been sent)

3. Flush the queue when an ARP reply arrives
in main() we will update the arp handling branch to:

// arp handling branch
if (ethernet.etherType() == ETHERTYPE_ARP)
{
    ArpHandler(ethernet, tap, ourIp, ourMac, arpCache, pendingArp);
    FlushPendingPackets(tap, arpCache, ourMac, pendingPackets);
    continue;
}

This ensures that after an ARP packet updates the cache, queued packets are checked for delivery.

important note: this only flushes packets when an ARP frame arrives. so I will still need to integrate it with an actual outgoing packet path.

## Next step: add an outgoing UDP function:

Now we need to connect the new queue to an actual outgoing packet path.
so in ipv4.hpp:

add this after createReply:

static Ipv4Packet create(
        std::uint32_t sourceIp,
        std::uint32_t destinationIp,
        std::uint8_t protocol,
        const std::vector<unsigned char> &payload);

then in ipv4.cpp add the implementation under createReply implementation:

Ipv4Packet Ipv4Packet::create(std::uint32_t sourceIp,
                              std::uint32_t destinationIp,
                              std::uint8_t protocol,
                              const std::vector<unsigned char> &payload)
{
    if (payload.size() > 65515)
    {
        throw std::length_error("IPv4 payload is too large");
    }
    Ipv4Packet packet;

    packet.version_ = 4;
    packet.header_length_ = 5;
    packet.tos_ = 0;
    packet.total_length_ = static_cast<std::uint16_t>(20 + payload.size());
    packet.identification_ = 0;
    packet.flags_ = 0;
    packet.fragment_offset_ = 0;
    packet.ttl_ = 64;
    packet.protocol_ = protocol;
    packet.source_ip_ = sourceIp;
    packet.destination_ip_ = destinationIp;
    packet.payload_ = payload;

    return packet;
}

Why this works?
- create() fills in the IPv4 header fields and stores the payload.
- serialize() builds the bytes and calculates the header checksum when the packet is serialized.
- For UDP, the protocol argument will be 17.
One important detail: this creates the IPv4 packet, but it doesn't send it.

## Next: create an outgoing UDP packet
We’ll connect the pieces I've built:
1. Create a UDP packet with source port, destination port, and payload.
2. Serialize the UDP packet.
3. Wrap it in IPv4 using your new Ipv4Packet::create().
4. Pass the serialized IPv4 packet to SendIpv4PacketTo(), which handles ARP resolution and queues the packet if the MAC address is unknown.

my udp files have everything i need to start:

Add one outgoing UDP helper to main.cpp that connects UDP creation, IPv4 encapsulation, and my existing ARP queue. That way, sending UDP won't need to know whether the destination MAC address is already cached.

we add in main.cpp under SendIpv4PacketWithARP():

void SendUdpPacket(
    TapDevice &tap,
    ArpCache &arpCache,
    const MacAddress &ourMac,
    std::uint32_t ourIp,
    std::uint32_t targetIp,
    std::uint16_t sourcePort,
    std::uint16_t destinationPort,
    const std::vector<unsigned char> &payload,
    std::unordered_set<std::uint32_t> &pendingRequests,
    std::vector<PendingPacket> &pendingPackets)
{

    // we create the UDP packet
    UdpPacket udp(sourcePort, destinationPort, payload);

    // calculate the checksum for the UDP packet using both the source and destination IP addresses
    udp.setChecksum(ourIp, targetIp);

    // serialize the UDP packet to get the raw bytes
    auto udpData = udp.serialize();

    // wrap the UDP packet in an IPv4 packet
    Ipv4Packet ipv4 = Ipv4Packet::create(ourIp, targetIp, IP_PROTOCOL_UDP, udpData);

    // serialize the complete IPv4 packet to get the raw bytes
    auto ipv4Data = ipv4.serialize();

    // send immediately if the MAC address is resolved, otherwise queue it for later sending
    SendIpv4PacketWithARP(tap, arpCache, ourMac, ourIp, targetIp, ipv4Data, pendingRequests, pendingPackets);
}

it will be like this:

+-------------------------------------------------------+
|                  Application payload                  |
|                  Our message as bytes                 |
+-------------------------------------------------------+
                            |
                            v
+-------------------------------------------------------+
|                      UDP packet                       |
|             Ports, length, checksum, payload          |
+-------------------------------------------------------+
                            |
                            v
+-------------------------------------------------------+
|                      IPv4 packet                      |
|    Source IP, destination IP, protocol 17, UDP data   |
+-------------------------------------------------------+
                            |
                            v
+--------------------------------------------------------+
|         ARP resolution -> Ethernet frame -> TAP        |
|   Send immediately or queue until the MAC is resolved  |
+--------------------------------------------------------+

this is what we call "Network Encapsulation"

------------------------------------------------------------------------------------------------------
# Step 1: Add the outgoing UDP message:

in main() above the while loop:

// send an initial UDP message to the TAP peer to demonstrate functionality
const std::uint32_t targetIp = (10u << 24) | (0u << 16) | (0u << 8) | 2u;

const std::vector<unsigned char> message = {
    'H', 'e', 'l', 'l', 'o', ' ', 'f', 'r', 'o', 'm', ' ',
    'm', 'y', ' ', 'T', 'C', 'P', '/', 'I', 'P', ' ', 's', 't', 'a', 'c', 'k'};

   SendUdpPacket(
    tap,
    arpCache,
    ourMac,
    ourIp,
    targetIp,
    8000,  // Source UDP port
    9000,  // Destination UDP port
    message,
    pendingArp,
    pendingPackets);

assumes TapDevice instance is named tap

# Step 2: Understand what happens:

1. Construct the UDP:
Builds a message from source port 8000 to destination port 9000, including its length and checksum.

2. Wrap it in IPv4:
Uses source IP 10.0.0.1, destination IP 10.0.0.2, and protocol number 17 for UDP.

3. Resolve the destination MAC:
If 10.0.0.2 isn't in the ARP cache, the stack sends an ARP request and queues the IPv4 packet.

4. Send the queued packet:
When the ARP reply arrives, my existing FlushPendingPackets() sends the IPv4 packet inside an Ethernet frame.

# Step 3: One thing to keep in mind:
This call sends the initial message once, when the program starts. My current ARP handler and queue should handle the unresolved destination while the receive loop is running.

The remote machine or application must listen on UDP port 9000 if I want to observe the message at the application level. The packet can still be transmitted without a listener, but I may not receive a response.

## Next step: add a UDP receive handler:

We'll keep the networking layers separate:
- IPv4 handler: identifies UDP traffic and passes the source/destination IP addresses and UDP payload to the UDP layer.
- UDP handler: parses the UDP header, validates the checksum, and reports the source port, destination port, and payload.
- Application layer: decides what to do with the payload, such as responding to messages sent to port 9000.

we need to separate UDP processing from application behavior.

above the ipv4 handler in main.cpp:

void HandleUdpPayload(const UdpPacket &udp)
{
    // this application handles messages addressed to port 9000
    if (udp.destinationPort() != 9000)
    {
        std::cout << "No application handler for UDP port "
                  << udp.destinationPort() << ".\n";
        return;
    }

    std::cout << "UDP message received on port 9000.\n";
    std::cout << "Message: ";
    
    const auto &payload = udp.payload();

    std::cout.write(
        reinterpret_cast<const char *>(payload.data()),
        static_cast<std::streamsize>(payload.size()));

    std::cout << "\n";
}
reinterpret_cast<const char *>(payload.data()) simply converts the pointer into another type which is character here. so it will become char.

example:

int x = 65;
int* p_int = &x;

// Reinterpret the integer pointer as a char pointer
char* p_char = reinterpret_cast<char*>(p_int); 

// Both pointers point to the exact same memory address, 
// but *p_char will read the first byte of that integer (printing 'A' on ASCII systems)


and this: static_cast<std::streamsize>(payload.size()) will basically convert the size into streamsize data type.

then in ipv4 handler we add: HandleUdpPayload(udp); after:

std::cout << "UDP Checksum Verified.\n";

std::cout << "Source Port: " << udp.sourcePort() << "\n";
std::cout << "Destination Port: " << udp.destinationPort() << "\n";
std::cout << "Payload Size: " << udp.payload().size() << "\n";
        
HandleUdpPayload(udp); --> here we add it

## we want to understand something about the two ports:

Outgoing destination port 9000: the port on the remote machine (10.0.0.2) that should receive my outgoing message.

Incoming destination port 9000: the port on my stack where this new handler processes received messages.

Those are separate directions. my outgoing message won't automatically be delivered to my own handler; the remote machine needs to send a UDP packet back to my stack for the incoming handler to process it.

## Next: add a UDP response
When it receives a valid UDP message on port 9000, it can send a response back to the sender.

the response will use: 
Source IP: 10.0.0.1
Destination IP: the incoming packet's source IP
Source UDP port: 9000
Destination UDP port: the incoming packet's source port
Payload: "UDP message received"

Ok since i need a response i need to update my HandleUdPayload and Ipv4Handler as i did in main.cpp in commit 90 on github: https://github.com/yaminmusleh/TCP-IP-Stack

our stack now has a complete basic UDP request-and-response path in the implementation:
1. It creates outgoing UDP packets.
2. It resolves destination MAC addresses through ARP and queues packets while resolution is pending.
3. It parses incoming UDP packets and verifies their checksums.
4. It delivers messages addressed to port 9000 to the application handler.
5. It constructs a response using the sender's IP address and UDP source port.

there is only two implementation steps to finish out udp and test it:

--------------------------------------------------------------------------------------------------------
## Next: harden UDP packet validation:
we need to fix some things:
1. UDP length validation
UdpPacket::parse() already checks that the UDP length is at least 8 bytes and doesn't exceed the available data. However, it currently accepts trailing bytes beyond the UDP length. We'll make the parsing behavior stricter so the UDP packet is interpreted consistently.

2. Malformed packets
Ipv4Handler() parses IPv4 and UDP packets, and these parsers can throw exceptions. A malformed packet should not terminate the entire stack.

so we make the parser stricter:

instead of this:
if (length > data.size())
{
    throw std::runtime_error("UDP packet is truncated");
}

we replace it with this:
if (length != data.size())
{
    throw std::runtime_error("UDP length does not match packet data size");
}
This ensures the UDP byte vector passed to parse() contains exactly the length declared by the UDP header. That matches my current IPv4 handler, which passes ipv4.payload() directly to UdpPacket::parse().

## Next: prevent malformed packets from stopping the stack:
IPv4 and UDP parsers can throw exceptions when a packet is invalid or truncated. At the moment, an exception could escape Ipv4Handler() and terminate my main receive loop.
using try catch we will have:

while (true)
    {
        auto raw_frame = tap.readFrame();

        try
        {
            EthernetFrame ethernet =
                EthernetFrame::parse(raw_frame);

            // ipv4 handling branch
            if (ethernet.etherType() == ETHERTYPE_IPV4)
            {
                Ipv4Handler(ethernet, tap, arpCache, ourMac, ourIp, pendingArp, pendingPackets);
                continue;
            }
            // arp handling branch
            if (ethernet.etherType() == ETHERTYPE_ARP)
            {
                ArpHandler(ethernet, tap, ourIp, ourMac, arpCache, pendingArp);
                FlushPendingPackets(tap, arpCache, ourMac, pendingPackets);
                continue;
            }
            // ipv6 handling branch
            if (ethernet.etherType() == ETHERTYPE_IPV6)
            {
                // ipv6 is not supported yet, so we will ignore it for now
                continue;
            }
            std::cout << "Unknown EtherType: 0x"
                      << std::hex
                      << ethernet.etherType()
                      << std::dec
                      << "\n";
        }
        catch (const std::exception &e)
        {
            std::cerr << "Dropping malformed packet: "
                      << e.what() << '\n';
        }
}

you can continue from here by yourself :)