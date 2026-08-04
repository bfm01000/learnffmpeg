#include "media_probe.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void print_usage(const char* argv0) {
  std::cout << "Usage:\n";
  std::cout << "  " << argv0 << " --input sample.mp4 --packets 80\n";
  std::cout << "  " << argv0 << " --input sample.mp4 --packets 80 --annexb-output sample.h264\n";
}

ProbeOptions parse_args(int argc, char** argv) {
  ProbeOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      std::exit(0);
    }
    if (arg == "--input" && i + 1 < argc) {
      options.input = argv[++i];
      continue;
    }
    if (arg == "--packets" && i + 1 < argc) {
      options.packets = std::stoi(argv[++i]);
      continue;
    }
    if (arg == "--annexb-output" && i + 1 < argc) {
      options.annexb_output = argv[++i];
      continue;
    }
    throw std::runtime_error("unknown argument: " + arg);
  }
  if (options.input.empty()) throw std::runtime_error("--input is required");
  if (options.packets <= 0) throw std::runtime_error("--packets must be positive");
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    probe_media(parse_args(argc, argv));
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
}
