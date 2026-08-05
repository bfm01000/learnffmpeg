cmake_minimum_required(VERSION 3.16)
project(epoll_poll_select_demo CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(epoll_poll_select_demo main.cpp)
