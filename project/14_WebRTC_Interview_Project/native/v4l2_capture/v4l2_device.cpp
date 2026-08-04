#include "v4l2_device.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {

int xioctl(int fd, unsigned long request, void* arg) {
  int result = 0;
  do {
    result = ioctl(fd, request, arg);
  } while (result == -1 && errno == EINTR);
  return result;
}

void throw_errno(const std::string& message) {
  throw std::runtime_error(message + ": " + std::strerror(errno));
}

}  // namespace

std::uint32_t pixel_format_from_name(const std::string& name) {
  if (name == "mjpeg" || name == "MJPEG") return V4L2_PIX_FMT_MJPEG;
  if (name == "yuyv" || name == "YUYV" || name == "yuyv422") return V4L2_PIX_FMT_YUYV;
  throw std::runtime_error("unsupported pixel format: " + name);
}

std::string pixel_format_to_string(std::uint32_t format) {
  char fourcc[5] = {
      static_cast<char>(format & 0xff),
      static_cast<char>((format >> 8) & 0xff),
      static_cast<char>((format >> 16) & 0xff),
      static_cast<char>((format >> 24) & 0xff),
      0,
  };
  return std::string(fourcc);
}

V4L2Device::V4L2Device(std::string device_path) : device_path_(std::move(device_path)) {}

V4L2Device::~V4L2Device() {
  try {
    close_device();
  } catch (...) {
  }
}

void V4L2Device::open_device() {
  fd_ = open(device_path_.c_str(), O_RDWR | O_NONBLOCK, 0);
  if (fd_ < 0) throw_errno("failed to open " + device_path_);
}

void V4L2Device::close_device() {
  if (streaming_) stream_off();
  release_buffers();
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
}

void V4L2Device::print_capabilities() const {
  v4l2_capability cap {};
  if (xioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) throw_errno("VIDIOC_QUERYCAP failed");

  std::cout << "driver       : " << cap.driver << "\n";
  std::cout << "card         : " << cap.card << "\n";
  std::cout << "bus_info     : " << cap.bus_info << "\n";
  std::cout << "capabilities : 0x" << std::hex << cap.capabilities << std::dec << "\n";

  if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
    throw std::runtime_error("device does not support video capture");
  }
  if ((cap.capabilities & V4L2_CAP_STREAMING) == 0) {
    throw std::runtime_error("device does not support streaming I/O");
  }
}

void V4L2Device::list_formats() const {
  print_capabilities();
  std::cout << "\nformats:\n";

  v4l2_fmtdesc fmt {};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  for (fmt.index = 0; xioctl(fd_, VIDIOC_ENUM_FMT, &fmt) == 0; ++fmt.index) {
    std::cout << "  " << pixel_format_to_string(fmt.pixelformat) << " - " << fmt.description << "\n";

    v4l2_frmsizeenum size {};
    size.pixel_format = fmt.pixelformat;
    for (size.index = 0; xioctl(fd_, VIDIOC_ENUM_FRAMESIZES, &size) == 0; ++size.index) {
      if (size.type != V4L2_FRMSIZE_TYPE_DISCRETE) continue;
      std::cout << "    " << size.discrete.width << "x" << size.discrete.height;

      v4l2_frmivalenum interval {};
      interval.pixel_format = fmt.pixelformat;
      interval.width = size.discrete.width;
      interval.height = size.discrete.height;
      bool first = true;
      for (interval.index = 0; xioctl(fd_, VIDIOC_ENUM_FRAMEINTERVALS, &interval) == 0; ++interval.index) {
        if (interval.type != V4L2_FRMIVAL_TYPE_DISCRETE) continue;
        if (first) {
          std::cout << " @ ";
          first = false;
        } else {
          std::cout << ", ";
        }
        if (interval.discrete.numerator != 0) {
          const double fps = static_cast<double>(interval.discrete.denominator) /
                             static_cast<double>(interval.discrete.numerator);
          std::cout << std::fixed << std::setprecision(2) << fps << "fps";
        }
      }
      std::cout << "\n";
    }
  }
}

void V4L2Device::set_format(std::uint32_t pixel_format, int width, int height) {
  v4l2_format fmt {};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = static_cast<std::uint32_t>(width);
  fmt.fmt.pix.height = static_cast<std::uint32_t>(height);
  fmt.fmt.pix.pixelformat = pixel_format;
  fmt.fmt.pix.field = V4L2_FIELD_ANY;

  if (xioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) throw_errno("VIDIOC_S_FMT failed");

  active_pixel_format_ = fmt.fmt.pix.pixelformat;
  active_width_ = static_cast<int>(fmt.fmt.pix.width);
  active_height_ = static_cast<int>(fmt.fmt.pix.height);

  std::cout << "active format: " << active_width_ << "x" << active_height_ << " "
            << pixel_format_to_string(active_pixel_format_) << "\n";
  std::cout << "bytesperline : " << fmt.fmt.pix.bytesperline << "\n";
  std::cout << "sizeimage    : " << fmt.fmt.pix.sizeimage << "\n";
}

void V4L2Device::request_buffers() {
  v4l2_requestbuffers req {};
  req.count = 4;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (xioctl(fd_, VIDIOC_REQBUFS, &req) < 0) throw_errno("VIDIOC_REQBUFS failed");
  if (req.count < 2) throw std::runtime_error("not enough V4L2 buffers");

  buffers_.resize(req.count);
  for (std::uint32_t i = 0; i < req.count; ++i) {
    v4l2_buffer buf {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;

    if (xioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) throw_errno("VIDIOC_QUERYBUF failed");
    buffers_[i].length = buf.length;
    buffers_[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buf.m.offset);
    if (buffers_[i].start == MAP_FAILED) throw_errno("mmap failed");
  }
}

void V4L2Device::release_buffers() {
  for (auto& buffer : buffers_) {
    if (buffer.start && buffer.start != MAP_FAILED) {
      munmap(buffer.start, buffer.length);
    }
  }
  buffers_.clear();
}

void V4L2Device::stream_on() {
  for (std::uint32_t i = 0; i < buffers_.size(); ++i) {
    v4l2_buffer buf {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;
    if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0) throw_errno("VIDIOC_QBUF failed");
  }

  v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0) throw_errno("VIDIOC_STREAMON failed");
  streaming_ = true;
}

void V4L2Device::stream_off() {
  v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (xioctl(fd_, VIDIOC_STREAMOFF, &type) < 0) throw_errno("VIDIOC_STREAMOFF failed");
  streaming_ = false;
}

CapturedFrame V4L2Device::capture_one_frame() {
  request_buffers();
  stream_on();

  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(fd_, &fds);

  timeval tv {};
  tv.tv_sec = 3;
  tv.tv_usec = 0;

  const int ready = select(fd_ + 1, &fds, nullptr, nullptr, &tv);
  if (ready < 0) throw_errno("select failed");
  if (ready == 0) throw std::runtime_error("capture timed out");

  v4l2_buffer buf {};
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  if (xioctl(fd_, VIDIOC_DQBUF, &buf) < 0) throw_errno("VIDIOC_DQBUF failed");

  CapturedFrame frame;
  frame.pixel_format = active_pixel_format_;
  frame.bytes_used = buf.bytesused;
  frame.sequence = buf.sequence;
  frame.timestamp_us = static_cast<std::int64_t>(buf.timestamp.tv_sec) * 1000000LL + buf.timestamp.tv_usec;
  frame.data.assign(static_cast<std::uint8_t*>(buffers_[buf.index].start),
                    static_cast<std::uint8_t*>(buffers_[buf.index].start) + buf.bytesused);

  std::cout << "captured frame:\n";
  std::cout << "  sequence     : " << frame.sequence << "\n";
  std::cout << "  bytes_used   : " << frame.bytes_used << "\n";
  std::cout << "  timestamp_us : " << frame.timestamp_us << "\n";
  std::cout << "  pixel_format : " << pixel_format_to_string(frame.pixel_format) << "\n";

  if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0) throw_errno("VIDIOC_QBUF return failed");
  stream_off();
  release_buffers();
  return frame;
}
