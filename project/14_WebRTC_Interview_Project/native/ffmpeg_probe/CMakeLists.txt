find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)

add_executable(ffmpeg_probe
  main.cpp
  media_probe.cpp
)

target_include_directories(ffmpeg_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ffmpeg_probe PRIVATE PkgConfig::FFMPEG)
target_compile_options(ffmpeg_probe PRIVATE -Wall -Wextra -Wpedantic)
