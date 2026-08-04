#include "annexb_reader.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace {

std::vector<std::uint8_t> read_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("failed to open input: " + path);
  return std::vector<std::uint8_t>(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
}

bool is_start_code(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::size_t* prefix_size) {
  if (offset + 3 <= bytes.size() && bytes[offset] == 0 && bytes[offset + 1] == 0 && bytes[offset + 2] == 1) {
    *prefix_size = 3;
    return true;
  }
  if (offset + 4 <= bytes.size() && bytes[offset] == 0 && bytes[offset + 1] == 0 &&
      bytes[offset + 2] == 0 && bytes[offset + 3] == 1) {
    *prefix_size = 4;
    return true;
  }
  return false;
}

std::vector<std::size_t> find_start_codes(const std::vector<std::uint8_t>& bytes) {
  std::vector<std::size_t> offsets;
  for (std::size_t i = 0; i + 3 < bytes.size(); ++i) {
    std::size_t prefix_size = 0;
    if (is_start_code(bytes, i, &prefix_size)) {
      offsets.push_back(i);
      i += prefix_size - 1;
    }
  }
  return offsets;
}

std::size_t trim_trailing_zero(const std::vector<std::uint8_t>& bytes, std::size_t begin, std::size_t end) {
  while (end > begin && bytes[end - 1] == 0) {
    end -= 1;
  }
  return end;
}

}  // namespace

const char* h264_nalu_type_name(int type) {
  switch (type) {
    case 1: return "non-IDR slice";
    case 5: return "IDR slice";
    case 6: return "SEI";
    case 7: return "SPS";
    case 8: return "PPS";
    case 9: return "AUD";
    default: return "other";
  }
}

std::vector<H264Nalu> read_annexb_nalus(const std::string& path) {
  const auto bytes = read_file(path);
  const auto offsets = find_start_codes(bytes);
  std::vector<H264Nalu> nalus;
  int frame_index = -1;

  for (std::size_t i = 0; i < offsets.size(); ++i) {
    std::size_t prefix_size = 0;
    if (!is_start_code(bytes, offsets[i], &prefix_size)) continue;

    const std::size_t payload_begin = offsets[i] + prefix_size;
    const std::size_t next = (i + 1 < offsets.size()) ? offsets[i + 1] : bytes.size();
    const std::size_t payload_end = trim_trailing_zero(bytes, payload_begin, next);
    if (payload_begin >= payload_end) continue;

    const int type = bytes[payload_begin] & 0x1f;
    if (type == 9 || frame_index < 0) {
      frame_index += 1;
    }

    H264Nalu nalu;
    nalu.offset = offsets[i];
    nalu.type = type;
    nalu.frame_index = frame_index;
    nalu.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(payload_begin),
                        bytes.begin() + static_cast<std::ptrdiff_t>(payload_end));
    nalus.push_back(std::move(nalu));
  }

  return nalus;
}
