#include "annexb_reader.h"
#include "rtp_packetizer.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {

void print_usage(const char* argv0) {
  std::cout << "Usage:\n";
  std::cout << "  " << argv0 << " --input sample.h264 --output rtp.csv\n";
  std::cout << "  " << argv0 << " --input sample.h264 --output rtp.csv --max-payload 1200 --fps 25\n";
}

struct CliOptions {
  std::string input;
  std::string output;
  RtpPacketizerOptions packetizer;
};

CliOptions parse_args(int argc, char** argv) {
  CliOptions options;
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
    if (arg == "--output" && i + 1 < argc) {
      options.output = argv[++i];
      continue;
    }
    if (arg == "--max-payload" && i + 1 < argc) {
      options.packetizer.max_payload_size = static_cast<std::size_t>(std::stoul(argv[++i]));
      continue;
    }
    if (arg == "--fps" && i + 1 < argc) {
      options.packetizer.fps = std::stoi(argv[++i]);
      continue;
    }
    if (arg == "--payload-type" && i + 1 < argc) {
      options.packetizer.payload_type = std::stoi(argv[++i]);
      continue;
    }
    if (arg == "--start-seq" && i + 1 < argc) {
      options.packetizer.start_sequence = static_cast<std::uint16_t>(std::stoul(argv[++i]));
      continue;
    }
    if (arg == "--start-ts" && i + 1 < argc) {
      options.packetizer.start_timestamp = static_cast<std::uint32_t>(std::stoul(argv[++i]));
      continue;
    }
    if (arg == "--keep-aud") {
      options.packetizer.skip_aud = false;
      continue;
    }
    throw std::runtime_error("unknown argument: " + arg);
  }
  if (options.input.empty()) throw std::runtime_error("--input is required");
  if (options.output.empty()) throw std::runtime_error("--output is required");
  return options;
}

void print_summary(const std::vector<H264Nalu>& nalus, const std::vector<RtpPacketRecord>& records) {
  std::map<int, int> nalu_counts;
  std::map<std::string, int> packetization_counts;
  int max_frame_index = -1;
  for (const auto& nalu : nalus) {
    nalu_counts[nalu.type] += 1;
    max_frame_index = std::max(max_frame_index, nalu.frame_index);
  }
  for (const auto& record : records) {
    packetization_counts[record.packetization] += 1;
  }

  std::cout << "nalu summary:\n";
  std::cout << "  frames       : " << (max_frame_index + 1) << "\n";
  std::cout << "  total_nalus  : " << nalus.size() << "\n";
  for (const auto& [type, count] : nalu_counts) {
    std::cout << "  type " << type << " (" << h264_nalu_type_name(type) << ") : " << count << "\n";
  }

  std::cout << "\nrtp packet summary:\n";
  std::cout << "  total_packets : " << records.size() << "\n";
  for (const auto& [mode, count] : packetization_counts) {
    std::cout << "  " << mode << " : " << count << "\n";
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const CliOptions options = parse_args(argc, argv);
    const auto nalus = read_annexb_nalus(options.input);
    const auto records = packetize_h264_to_rtp(nalus, options.packetizer);

    std::ofstream output(options.output);
    if (!output) throw std::runtime_error("failed to open output: " + options.output);
    write_rtp_csv(records, output);

    print_summary(nalus, records);
    std::cout << "  csv_output    : " << options.output << "\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
}
