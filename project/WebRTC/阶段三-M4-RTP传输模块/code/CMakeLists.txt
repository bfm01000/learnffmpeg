cmake_minimum_required(VERSION 3.14)
project(MyWebRtcRtpModule CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0)
    if(NOT WIN32)
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif()
endif()

add_library(my_rtp_module STATIC
    src/rtp_packet.cc
    src/h264_packetizer.cc
    src/h264_depacketizer.cc
)
target_include_directories(my_rtp_module PUBLIC include)

enable_testing()
add_subdirectory(tests)
