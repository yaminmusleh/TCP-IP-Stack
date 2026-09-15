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