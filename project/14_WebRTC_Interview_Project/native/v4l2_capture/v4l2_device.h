#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct CapturedFrame {
  std::vector<std::uint8_t> data;
  std::uint32_t pixel_format = 0;
  std::uint32_t bytes_used = 0;
  std::uint32_t sequence = 0;
  std::int64_t timestamp_us = 0;
};

class V4L2Device {
 public:
  explicit V4L2Device(std::string device_path);
  ~V4L2Device();

  V4L2Device(const V4L2Device&) = delete;
  V4L2Device& operator=(const V4L2Device&) = delete;

  void open_device();
  void close_device();
  void print_capabilities() const;
  void list_formats() const;
  void set_format(std::uint32_t pixel_format, int width, int height);
  CapturedFrame capture_one_frame();

 private:
  struct Buffer {
    void* start = nullptr;
    std::size_t length = 0;
  };

  void request_buffers();
  void release_buffers();
  void stream_on();
  void stream_off();

  std::string device_path_;
  int fd_ = -1;
  std::vector<Buffer> buffers_;
  std::uint32_t active_pixel_format_ = 0;
  int active_width_ = 0;
  int active_height_ = 0;
  bool streaming_ = false;
};

std::uint32_t pixel_format_from_name(const std::string& name);
std::string pixel_format_to_string(std::uint32_t format);
