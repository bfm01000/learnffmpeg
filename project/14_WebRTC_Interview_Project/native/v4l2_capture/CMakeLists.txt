add_executable(v4l2_capture
  main.cpp
  v4l2_device.cpp
  image_writer.cpp
)

target_include_directories(v4l2_capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(v4l2_capture PRIVATE -Wall -Wextra -Wpedantic)
