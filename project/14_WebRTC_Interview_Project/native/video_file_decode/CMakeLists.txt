find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)

add_executable(video_file_decode
  main.cpp
)

target_link_libraries(video_file_decode PRIVATE PkgConfig::FFMPEG)
target_compile_options(video_file_decode PRIVATE -Wall -Wextra -Wpedantic)