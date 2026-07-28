cmake_minimum_required(VERSION 3.16)
project(linux-player-sdk VERSION 0.1.0 LANGUAGES C CXX)

# ── C++17 ──────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Build type ─────────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# ── CMake module path ──────────────────────────────────────────────────────
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ── Options ────────────────────────────────────────────────────────────────
option(BUILD_SHARED_LIBS    "Build shared library"        ON)
option(BUILD_TESTS          "Build unit / integration tests" ON)
option(BUILD_EXAMPLES       "Build example applications"  ON)
option(ENABLE_HWACCEL_VAAPI "Enable VAAPI hardware decode" ON)
option(ENABLE_HWACCEL_VDPAU "Enable VDPAU hardware decode" OFF)
option(ENABLE_HWACCEL_CUDA  "Enable CUDA hardware decode"  OFF)
option(ENABLE_PLUGIN_RTMP   "Enable RTMP protocol plugin"  ON)
option(ENABLE_PLUGIN_RTSP   "Enable RTSP protocol plugin"  ON)
option(ENABLE_PLUGIN_SRT    "Enable SRT protocol plugin"   OFF)  # future
option(ENABLE_PLUGIN_WEBRTC "Enable WebRTC plugin"         OFF)  # future
option(ENABLE_PLUGIN_HLS    "Enable HLS protocol plugin"   ON)
option(ENABLE_SANITIZERS    "Enable ASan / UBSan"          OFF)
option(ENABLE_LTO           "Enable link-time optimization" OFF)

# ── Find dependencies ──────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)

# FFmpeg
pkg_check_modules(FFMPEG REQUIRED
  libavformat libavcodec libavutil libswscale libswresample libavfilter)

# SDL2
pkg_check_modules(SDL2 REQUIRED sdl2)

# OpenGL
find_package(OpenGL REQUIRED)

# GLFW (windowing) — optional, only needed for OpenGL video renderer
pkg_check_modules(GLFW QUIET glfw3)
if(GLFW_FOUND)
  add_compile_definitions(PLAYER_HAS_GLFW=1)
else()
  message(WARNING "glfw3 not found — OpenGL window renderer disabled")
endif()

# Threads
find_package(Threads REQUIRED)

# ── Sanitizers ─────────────────────────────────────────────────────────────
if(ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address -fsanitize=undefined)
endif()

# ── LTO ────────────────────────────────────────────────────────────────────
if(ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)
  if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif()

# ── Common compile flags ───────────────────────────────────────────────────
add_compile_options(
  -Wall
  -Wextra
  -Wpedantic
  -Wno-unused-parameter
  -Werror=return-type
)

# PIC required for shared library
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── Sub-libraries ──────────────────────────────────────────────────────────
add_subdirectory(core)
add_subdirectory(source)
add_subdirectory(decode)
add_subdirectory(process)
add_subdirectory(render)
add_subdirectory(control)
add_subdirectory(utils)

# ── Public API (header-only) ───────────────────────────────────────────────
add_library(api INTERFACE)
target_include_directories(api INTERFACE
  ${CMAKE_SOURCE_DIR}/api
)

# ── SDK aggregate library ──────────────────────────────────────────────────
add_library(player_sdk
  $<TARGET_OBJECTS:core>
  $<TARGET_OBJECTS:source>
  $<TARGET_OBJECTS:decode>
  $<TARGET_OBJECTS:process>
  $<TARGET_OBJECTS:render>
  $<TARGET_OBJECTS:control>
  $<TARGET_OBJECTS:utils>
)

target_link_libraries(player_sdk PUBLIC
  api
  core
  source
  decode
  process
  render
  control
  utils
  ${FFMPEG_LIBRARIES}
  ${SDL2_LIBRARIES}
  Threads::Threads
)

if(GLFW_FOUND)
  target_link_libraries(player_sdk PUBLIC OpenGL::GL ${GLFW_LIBRARIES})
endif()

target_include_directories(player_sdk PUBLIC
  ${CMAKE_SOURCE_DIR}
)

target_compile_options(player_sdk PRIVATE
  $<$<CONFIG:Debug>:-O0 -g3>
  $<$<CONFIG:RelWithDebInfo>:-O2 -g>
  $<$<CONFIG:Release>:-O3>
)

set_target_properties(player_sdk PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION 0
  OUTPUT_NAME player_sdk
)

# ── Install ────────────────────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS player_sdk
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY api/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/player_sdk
  FILES_MATCHING PATTERN "*.h")

# ── Tests ──────────────────────────────────────────────────────────────────
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

# ── Examples ───────────────────────────────────────────────────────────────
if(BUILD_EXAMPLES AND GLFW_FOUND)
  add_subdirectory(examples)
elseif(BUILD_EXAMPLES)
  message(WARNING "Examples disabled — GLFW not found")
endif()
