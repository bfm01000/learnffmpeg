cmake_minimum_required(VERSION 3.16)

project(media_core CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(
    FFMPEG REQUIRED
    libavformat
    libavcodec
    libavutil
    libswscale
)

add_library(media_core STATIC
    src/ffmpeg_components.cpp
    src/transcode_pipeline.cpp
)

target_include_directories(media_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${FFMPEG_INCLUDE_DIRS}
)

target_link_directories(media_core PUBLIC ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(media_core PUBLIC ${FFMPEG_LIBRARIES})
target_compile_options(media_core PUBLIC ${FFMPEG_CFLAGS_OTHER})

add_executable(media_core_demo demo/transcode_demo.cpp)
target_link_libraries(media_core_demo PRIVATE media_core)
