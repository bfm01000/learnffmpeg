#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct H264Nalu {
  std::size_t offset = 0;
  std::vector<std::uint8_t> payload;
  int type = 0;
  int frame_index = 0;
};

std::vector<H264Nalu> read_annexb_nalus(const std::string& path);
const char* h264_nalu_type_name(int type);
