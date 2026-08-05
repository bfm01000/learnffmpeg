cmake_minimum_required(VERSION 3.16)
project(lowlatency_webrtc_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(v4l2_capture)
add_subdirectory(ffmpeg_probe)
add_subdirectory(h264_rtp_packetizer)
add_subdirectory(video_file_decode)
