cmake_minimum_required(VERSION 3.16)

project(video2Image CXX)

find_package(PkgConfig REQUIRED)


pkg_check_modules(
    FFMPEG REQUIRED
    libavformat
    libavcodec
    libavutil
    libswresample
    libswscale
)

add_executable(video2Image main.cpp)

target_include_directories(video2Image PRIVATE ${FFMPEG_INCLUDE_DIRS})

target_link_directories(video2Image PRIVATE ${FFMPEG_LIBRARY_DIRS})

target_link_libraries(video2Image PRIVATE ${FFMPEG_LIBRARIES})

target_compile_options(video2Image PRIVATE ${FFMPEG_CFLAGS_OTHER})

