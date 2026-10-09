cmake_minimum_required(VERSION 3.20)

project(tcp_stack
    VERSION 0.1
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(tcp_stack
    src/main.cpp
    src/net/tap.cpp
    src/net/ethernet.cpp
    src/net/arp.cpp
    src/net/arp_cache.cpp
    src/net/ipv4.cpp
    src/net/icmpv4.cpp
    src/net/checksum.cpp
    src/net/udp.cpp
)

target_include_directories(tcp_stack
    PRIVATE
        include
)
target_compile_options(tcp_stack PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_executable(udp_test
    tests/udp_test.cpp
    src/net/udp.cpp
    src/net/checksum.cpp
)

target_include_directories(udp_test
    PRIVATE
        include
)

target_compile_options(udp_test PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)