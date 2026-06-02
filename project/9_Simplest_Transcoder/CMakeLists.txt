cmake_minimum_required(VERSION 3.16)
project(simplest_transcoder CXX)

# 生成 compile_commands.json,clangd/Cursor 跳转补全靠它(见 01 §8.3)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 转码器不需要 SDL(不显示),只用 FFmpeg。libswscale 做降分辨率缩放。
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libavutil libswscale)

add_executable(simplest_transcoder main.cpp)
target_include_directories(simplest_transcoder PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(simplest_transcoder PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(simplest_transcoder PRIVATE ${FFMPEG_LIBRARIES})
target_compile_options(simplest_transcoder PRIVATE ${FFMPEG_CFLAGS_OTHER})
