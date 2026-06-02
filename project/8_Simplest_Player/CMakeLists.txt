cmake_minimum_required(VERSION 3.16)
project(simplest_player CXX)

# 生成 compile_commands.json,clangd/Cursor 跳转补全靠它(见 01 §8.3)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# FFmpeg / SDL2 在 macOS / Linux(含 WSL2) 上都用 pkg-config 暴露元信息(见 01 §8.3)
# libswresample：音频重采样(接音频 A2 起用到)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libavutil libswscale libswresample)
pkg_check_modules(SDL2 REQUIRED sdl2)

add_executable(simplest_player main.cpp)
target_include_directories(simplest_player PRIVATE ${FFMPEG_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})
target_link_directories(simplest_player PRIVATE ${FFMPEG_LIBRARY_DIRS} ${SDL2_LIBRARY_DIRS})
target_link_libraries(simplest_player PRIVATE ${FFMPEG_LIBRARIES} ${SDL2_LIBRARIES})
target_compile_options(simplest_player PRIVATE ${FFMPEG_CFLAGS_OTHER} ${SDL2_CFLAGS_OTHER})
