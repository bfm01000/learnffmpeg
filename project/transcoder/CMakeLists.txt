cmake_minimum_required(VERSION 3.16)
project(transcoder CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(
  FFMPEG REQUIRED
  libavformat
  libavcodec
  libavutil
  libswresample
  libswscale
)

add_executable(transcoder src/main.cpp)
target_include_directories(transcoder PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(transcoder PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(transcoder PRIVATE ${FFMPEG_LIBRARIES})
target_compile_options(transcoder PRIVATE ${FFMPEG_CFLAGS_OTHER})
