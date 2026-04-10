cmake_minimum_required(VERSION 3.16)

project(tanscode CXX)

find_package(PkgConfig REQUIRED)

pkg_check_modules(
    FFMPEG REQUIRED
    libavformat
    libavcodec
    libavutil
    libswresample
    libswscale
)

add_executable(tanscode main.cpp)

set_target_properties(tanscode PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_include_directories(tanscode PRIVATE ${FFMPEG_INCLUDE_DIRS})

target_link_directories(tanscode PRIVATE ${FFMPEG_LIBRARY_DIRS})

target_link_libraries(tanscode PRIVATE ${FFMPEG_LIBRARIES})

target_compile_options(tanscode PRIVATE ${FFMPEG_CFLAGS_OTHER})