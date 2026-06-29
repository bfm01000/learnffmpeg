cmake_minimum_required(VERSION 3.16)
project(hw_codec_demo CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# Platform detection
# ============================================================
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_LINUX ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PLATFORM_MACOS ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(PLATFORM_ANDROID ON)
endif()

# ============================================================
# FFmpeg (required on all platforms for demux/mux)
# ============================================================
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavformat
    libavcodec
    libavutil
    libswscale
    libavfilter
)

# ============================================================
# NVIDIA (Linux only — CUDA + NVENC/NVDEC via FFmpeg)
# ============================================================
if(PLATFORM_LINUX)
    # Check if FFmpeg was built with CUDA support
    # hwcontext_cuda, h264_cuvid, hevc_cuvid decoders
    # h264_nvenc, hevc_nvenc encoders
    # scale_cuda filter
    message(STATUS "Platform: Linux — building NVIDIA CUDA hardware codec demos")

    add_executable(nvidia_hw_codec
        demo/nvidia_hw_codec.cpp
    )
    target_include_directories(nvidia_hw_codec PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_directories(nvidia_hw_codec PRIVATE ${FFMPEG_LIBRARY_DIRS})
    target_link_libraries(nvidia_hw_codec PRIVATE ${FFMPEG_LIBRARIES})
    target_compile_options(nvidia_hw_codec PRIVATE ${FFMPEG_CFLAGS_OTHER})
endif()

# ============================================================
# Apple (macOS only — VideoToolbox via FFmpeg)
# ============================================================
if(PLATFORM_MACOS)
    message(STATUS "Platform: macOS — building VideoToolbox hardware codec demos")

    add_executable(apple_hw_codec
        demo/apple_hw_codec.cpp
    )
    target_include_directories(apple_hw_codec PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_directories(apple_hw_codec PRIVATE ${FFMPEG_LIBRARY_DIRS})
    target_link_libraries(apple_hw_codec PRIVATE
        ${FFMPEG_LIBRARIES}
        "-framework VideoToolbox"
        "-framework CoreVideo"
        "-framework CoreMedia"
        "-framework Metal"
    )
    target_compile_options(apple_hw_codec PRIVATE ${FFMPEG_CFLAGS_OTHER})
endif()

# ============================================================
# Android (cross-compile only — AMediaCodec via NDK)
# ============================================================
# Android builds use a separate toolchain file.
# The android_hw_codec.cpp file is provided as reference;
# it compiles only with the Android NDK's CMake toolchain.
if(PLATFORM_ANDROID)
    message(STATUS "Platform: Android — building AMediaCodec hardware codec demos")

    find_library(mediandk mediandk REQUIRED)

    add_executable(android_hw_codec
        demo/android_hw_codec.cpp
    )
    target_include_directories(android_hw_codec PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_directories(android_hw_codec PRIVATE ${FFMPEG_LIBRARY_DIRS})
    target_link_libraries(android_hw_codec PRIVATE
        ${FFMPEG_LIBRARIES}
        ${mediandk}
    )
    target_compile_options(android_hw_codec PRIVATE ${FFMPEG_CFLAGS_OTHER})
endif()
