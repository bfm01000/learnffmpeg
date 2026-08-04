#pragma once

#include "annexb_reader.h"

#include <cstdint>
#include <ostream>
#include <vector>

struct RtpPacketizerOptions {
  std::size_t max_payload_size = 1200;
  std::uint16_t start_sequence = 1000;
  std::uint32_t start_timestamp = 90000;
  int payload_type = 96;
  int fps = 25;
  bool skip_aud = true;
};

struct RtpPacketRecord {
  int packet_index = 0;
  int frame_index = 0;
  std::uint16_t sequence_number = 0;
  std::uint32_t timestamp = 0;
  bool marker = false;
  int payload_type = 96;
  int nalu_type = 0;
  const char* nalu_name = "";
  const char* packetization = "";
  std::size_t payload_size = 0;
  bool fu_start = false;
  bool fu_end = false;
};

std::vector<RtpPacketRecord> packetize_h264_to_rtp(const std::vector<H264Nalu>& nalus,
                                                   const RtpPacketizerOptions& options);
void write_rtp_csv(const std::vector<RtpPacketRecord>& records, std::ostream& output);
