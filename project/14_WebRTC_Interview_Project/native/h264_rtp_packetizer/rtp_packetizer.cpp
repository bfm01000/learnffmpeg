#include "rtp_packetizer.h"

#include <algorithm>
#include <stdexcept>

namespace {

bool should_send(const H264Nalu& nalu, const RtpPacketizerOptions& options) {
  return !(options.skip_aud && nalu.type == 9);
}

bool is_last_sendable_nalu_in_frame(const std::vector<H264Nalu>& nalus,
                                    std::size_t current,
                                    const RtpPacketizerOptions& options) {
  const int frame_index = nalus[current].frame_index;
  for (std::size_t i = current + 1; i < nalus.size(); ++i) {
    if (nalus[i].frame_index != frame_index) break;
    if (should_send(nalus[i], options)) return false;
  }
  return true;
}

std::uint32_t timestamp_for_frame(int frame_index, const RtpPacketizerOptions& options) {
  const std::uint32_t step = static_cast<std::uint32_t>(90000 / std::max(options.fps, 1));
  return options.start_timestamp + static_cast<std::uint32_t>(frame_index) * step;
}

void append_record(std::vector<RtpPacketRecord>& records,
                   int frame_index,
                   std::uint16_t sequence_number,
                   std::uint32_t timestamp,
                   bool marker,
                   const RtpPacketizerOptions& options,
                   int nalu_type,
                   const char* packetization,
                   std::size_t payload_size,
                   bool fu_start,
                   bool fu_end) {
  RtpPacketRecord record;
  record.packet_index = static_cast<int>(records.size());
  record.frame_index = frame_index;
  record.sequence_number = sequence_number;
  record.timestamp = timestamp;
  record.marker = marker;
  record.payload_type = options.payload_type;
  record.nalu_type = nalu_type;
  record.nalu_name = h264_nalu_type_name(nalu_type);
  record.packetization = packetization;
  record.payload_size = payload_size;
  record.fu_start = fu_start;
  record.fu_end = fu_end;
  records.push_back(record);
}

}  // namespace

std::vector<RtpPacketRecord> packetize_h264_to_rtp(const std::vector<H264Nalu>& nalus,
                                                   const RtpPacketizerOptions& options) {
  if (options.max_payload_size < 3) {
    throw std::runtime_error("--max-payload must be at least 3 bytes for FU-A");
  }

  std::vector<RtpPacketRecord> records;
  std::uint16_t sequence = options.start_sequence;

  for (std::size_t i = 0; i < nalus.size(); ++i) {
    const H264Nalu& nalu = nalus[i];
    if (!should_send(nalu, options)) continue;

    const bool last_nalu_in_frame = is_last_sendable_nalu_in_frame(nalus, i, options);
    const std::uint32_t timestamp = timestamp_for_frame(nalu.frame_index, options);

    if (nalu.payload.size() <= options.max_payload_size) {
      append_record(records, nalu.frame_index, sequence++, timestamp, last_nalu_in_frame,
                    options, nalu.type, "single-nalu", nalu.payload.size(), false, false);
      continue;
    }

    const std::size_t fragment_capacity = options.max_payload_size - 2;
    std::size_t offset = 1;
    bool first = true;
    while (offset < nalu.payload.size()) {
      const std::size_t remaining = nalu.payload.size() - offset;
      const std::size_t fragment_size = std::min(fragment_capacity, remaining);
      const bool last_fragment = offset + fragment_size >= nalu.payload.size();
      append_record(records, nalu.frame_index, sequence++, timestamp,
                    last_nalu_in_frame && last_fragment, options, nalu.type, "fu-a",
                    fragment_size + 2, first, last_fragment);
      first = false;
      offset += fragment_size;
    }
  }

  return records;
}

void write_rtp_csv(const std::vector<RtpPacketRecord>& records, std::ostream& output) {
  output << "packet_index,frame_index,sequence_number,timestamp,marker,payload_type,nalu_type,nalu_name,packetization,payload_size,fu_start,fu_end\n";
  for (const auto& record : records) {
    output << record.packet_index << ","
           << record.frame_index << ","
           << record.sequence_number << ","
           << record.timestamp << ","
           << (record.marker ? 1 : 0) << ","
           << record.payload_type << ","
           << record.nalu_type << ","
           << record.nalu_name << ","
           << record.packetization << ","
           << record.payload_size << ","
           << (record.fu_start ? 1 : 0) << ","
           << (record.fu_end ? 1 : 0) << "\n";
  }
}
