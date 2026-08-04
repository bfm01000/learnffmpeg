add_executable(h264_rtp_packetizer
  main.cpp
  annexb_reader.cpp
  rtp_packetizer.cpp
)

target_include_directories(h264_rtp_packetizer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
