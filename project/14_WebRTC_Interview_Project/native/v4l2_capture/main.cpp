#include "image_writer.h"
#include "v4l2_device.h"

#include <linux/videodev2.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

struct Options {
  std::string device = "/dev/video0";
  std::string format = "mjpeg";
  int width = 640;
  int height = 480;
  std::string output = "../captures/native-camera.jpg";
  bool list = false;
};

void print_usage(const char* argv0) {
  std::cout << "Usage:\n";
  std::cout << "  " << argv0 << " --device /dev/video0 --list\n";
  std::cout << "  " << argv0 << " --device /dev/video0 --format mjpeg --size 640x480 --output ../captures/native-camera.jpg\n";
  std::cout << "  " << argv0 << " --device /dev/video0 --format yuyv --size 640x480 --output ../captures/native-camera.ppm\n";
}

Options parse_args(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      std::exit(0);
    }
    if (arg == "--list") {
      options.list = true;
      continue;
    }
    if (arg == "--device" && i + 1 < argc) {
      options.device = argv[++i];
      continue;
    }
    if (arg == "--format" && i + 1 < argc) {
      options.format = argv[++i];
      continue;
    }
    if (arg == "--size" && i + 1 < argc) {
      const std::string size = argv[++i];
      const auto x = size.find('x');
      if (x == std::string::npos) throw std::runtime_error("size must be WIDTHxHEIGHT");
      options.width = std::stoi(size.substr(0, x));
      options.height = std::stoi(size.substr(x + 1));
      continue;
    }
    if (arg == "--output" && i + 1 < argc) {
      options.output = argv[++i];
      continue;
    }
    throw std::runtime_error("unknown argument: " + arg);
  }
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = parse_args(argc, argv);
    V4L2Device device(options.device);
    device.open_device();

    if (options.list) {
      device.list_formats();
      return 0;
    }

    const std::uint32_t pixel_format = pixel_format_from_name(options.format);
    device.set_format(pixel_format, options.width, options.height);
    const CapturedFrame frame = device.capture_one_frame();

    if (frame.pixel_format == V4L2_PIX_FMT_MJPEG) {
      write_binary_file(options.output, frame.data.data(), frame.data.size());
    } else if (frame.pixel_format == V4L2_PIX_FMT_YUYV) {
      write_yuyv_as_ppm(options.output, frame.data, options.width, options.height);
    } else {
      throw std::runtime_error("capture succeeded but output writer does not support " +
                               pixel_format_to_string(frame.pixel_format));
    }

    std::cout << "saved        : " << options.output << "\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
}
