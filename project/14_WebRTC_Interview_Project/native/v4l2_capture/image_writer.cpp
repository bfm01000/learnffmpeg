#include "image_writer.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace {

std::uint8_t clamp_to_u8(int value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

void yuv_to_rgb(int y, int u, int v, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) {
  const int c = y - 16;
  const int d = u - 128;
  const int e = v - 128;
  r = clamp_to_u8((298 * c + 409 * e + 128) >> 8);
  g = clamp_to_u8((298 * c - 100 * d - 208 * e + 128) >> 8);
  b = clamp_to_u8((298 * c + 516 * d + 128) >> 8);
}

}  // namespace

void write_binary_file(const std::string& path, const void* data, std::size_t size) {
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("failed to open output file: " + path);
  }
  output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
}

void write_yuyv_as_ppm(const std::string& path, const std::vector<std::uint8_t>& data, int width, int height) {
  const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 2;
  if (data.size() < expected) {
    throw std::runtime_error("not enough YUYV data for requested image size");
  }

  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("failed to open output file: " + path);
  }

  output << "P6\n" << width << " " << height << "\n255\n";
  for (std::size_t i = 0; i + 3 < expected; i += 4) {
    const int y0 = data[i + 0];
    const int u = data[i + 1];
    const int y1 = data[i + 2];
    const int v = data[i + 3];

    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    yuv_to_rgb(y0, u, v, r, g, b);
    output.put(static_cast<char>(r));
    output.put(static_cast<char>(g));
    output.put(static_cast<char>(b));

    yuv_to_rgb(y1, u, v, r, g, b);
    output.put(static_cast<char>(r));
    output.put(static_cast<char>(g));
    output.put(static_cast<char>(b));
  }
}
