cmake_minimum_required(VERSION 3.16)
project(frame_index_demo CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libavutil libswscale)

add_executable(frame_index_demo main.cpp)
target_include_directories(frame_index_demo PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(frame_index_demo PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(frame_index_demo PRIVATE ${FFMPEG_LIBRARIES})
target_compile_options(frame_index_demo PRIVATE ${FFMPEG_CFLAGS_OTHER})
