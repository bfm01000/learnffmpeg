#include "media_probe.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct FormatContextGuard {
  AVFormatContext* context = nullptr;
  ~FormatContextGuard() {
    if (context) avformat_close_input(&context);
  }
};

struct PacketGuard {
  AVPacket* packet = av_packet_alloc();
  ~PacketGuard() {
    if (packet) av_packet_free(&packet);
  }
};

struct BsfGuard {
  AVBSFContext* context = nullptr;

  BsfGuard() = default;
  ~BsfGuard() {
    if (context) av_bsf_free(&context);
  }

  BsfGuard(const BsfGuard&) = delete;
  BsfGuard& operator=(const BsfGuard&) = delete;

  BsfGuard(BsfGuard&& other) noexcept : context(other.context) {
    other.context = nullptr;
  }

  BsfGuard& operator=(BsfGuard&& other) noexcept {
    if (this == &other) return *this;
    if (context) av_bsf_free(&context);
    context = other.context;
    other.context = nullptr;
    return *this;
  }
};

struct NaluStats {
  std::array<int, 32> counts {};
  int total = 0;
};

std::string av_error_to_string(int error) {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(error, buffer, sizeof(buffer));
  return std::string(buffer);
}

double rational_to_double(AVRational value) {
  if (value.den == 0) return 0.0;
  return static_cast<double>(value.num) / static_cast<double>(value.den);
}

double timestamp_to_seconds(std::int64_t timestamp, AVRational time_base) {
  if (timestamp == AV_NOPTS_VALUE) return 0.0;
  return static_cast<double>(timestamp) * rational_to_double(time_base);
}

std::string timestamp_to_text(std::int64_t timestamp, AVRational time_base) {
  if (timestamp == AV_NOPTS_VALUE) return "NOPTS";
  std::ostringstream output;
  output << timestamp << " (" << std::fixed << std::setprecision(6)
         << timestamp_to_seconds(timestamp, time_base) << "s)";
  return output.str();
}

std::string media_type_name(AVMediaType type) {
  const char* name = av_get_media_type_string(type);
  return name ? std::string(name) : "unknown";
}

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

void print_stream_info(const AVFormatContext* format_context) {
  std::cout << "input     : " << (format_context->url ? format_context->url : "") << "\n";
  std::cout << "format    : " << format_context->iformat->long_name << "\n";
  std::cout << "duration  : ";
  if (format_context->duration == AV_NOPTS_VALUE) {
    std::cout << "unknown\n";
  } else {
    std::cout << std::fixed << std::setprecision(3)
              << static_cast<double>(format_context->duration) / AV_TIME_BASE << "s\n";
  }
  std::cout << "bitrate   : " << format_context->bit_rate << "\n";
  std::cout << "streams   : " << format_context->nb_streams << "\n\n";

  for (unsigned int i = 0; i < format_context->nb_streams; ++i) {
    const AVStream* stream = format_context->streams[i];
    const AVCodecParameters* params = stream->codecpar;
    std::cout << "stream #" << i << "\n";
    std::cout << "  type      : " << media_type_name(params->codec_type) << "\n";
    std::cout << "  codec     : " << avcodec_get_name(params->codec_id) << "\n";
    std::cout << "  time_base : " << stream->time_base.num << "/" << stream->time_base.den << "\n";
    std::cout << "  extradata : " << params->extradata_size << " bytes";
    if (params->codec_id == AV_CODEC_ID_H264 && params->extradata_size > 0) {
      const bool looks_avcc = params->extradata[0] == 1;
      std::cout << (looks_avcc ? " (looks like AVCC)" : " (not AVCC header)");
    }
    std::cout << "\n";
    if (params->codec_type == AVMEDIA_TYPE_VIDEO) {
      std::cout << "  size      : " << params->width << "x" << params->height << "\n";
      std::cout << "  fps       : " << std::fixed << std::setprecision(3)
                << rational_to_double(stream->avg_frame_rate) << "\n";
      std::cout << "  pix_fmt   : " << params->format << "\n";
    }
    if (params->codec_type == AVMEDIA_TYPE_AUDIO) {
      std::cout << "  sample_rate: " << params->sample_rate << "\n";
      std::cout << "  channels   : " << params->ch_layout.nb_channels << "\n";
    }
    std::cout << "\n";
  }
}

int find_video_stream(const AVFormatContext* format_context) {
  for (unsigned int i = 0; i < format_context->nb_streams; ++i) {
    if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

std::vector<std::size_t> find_annexb_start_codes(const std::uint8_t* data, std::size_t size) {
  std::vector<std::size_t> offsets;
  for (std::size_t i = 0; i + 3 < size; ++i) {
    if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
      offsets.push_back(i);
      i += 2;
    } else if (i + 4 < size && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 1) {
      offsets.push_back(i);
      i += 3;
    }
  }
  return offsets;
}

std::size_t start_code_size(const std::uint8_t* data, std::size_t offset, std::size_t size) {
  if (offset + 3 <= size && data[offset] == 0 && data[offset + 1] == 0 && data[offset + 2] == 1) return 3;
  return 4;
}

void collect_nalu_stats(const std::uint8_t* data, std::size_t size, NaluStats& stats) {
  const auto offsets = find_annexb_start_codes(data, size);
  for (std::size_t offset : offsets) {
    const std::size_t header = offset + start_code_size(data, offset, size);
    if (header >= size) continue;
    const int type = data[header] & 0x1f;
    if (type >= 0 && type < static_cast<int>(stats.counts.size())) {
      stats.counts[type] += 1;
      stats.total += 1;
    }
  }
}

void print_nalu_stats(const NaluStats& stats) {
  std::cout << "\nannex-b nalu summary:\n";
  std::cout << "  total_nalus : " << stats.total << "\n";
  for (std::size_t i = 0; i < stats.counts.size(); ++i) {
    if (stats.counts[i] == 0) continue;
    std::cout << "  type " << i << " (" << h264_nalu_type_name(static_cast<int>(i)) << ") : " << stats.counts[i] << "\n";
  }
}

BsfGuard create_h264_mp4toannexb_filter(const AVCodecParameters* params) {
  const AVBitStreamFilter* filter = av_bsf_get_by_name("h264_mp4toannexb");
  if (!filter) throw std::runtime_error("h264_mp4toannexb bitstream filter not found");

  BsfGuard guard;
  int result = av_bsf_alloc(filter, &guard.context);
  if (result < 0) throw std::runtime_error("av_bsf_alloc failed: " + av_error_to_string(result));

  result = avcodec_parameters_copy(guard.context->par_in, params);
  if (result < 0) throw std::runtime_error("avcodec_parameters_copy failed: " + av_error_to_string(result));

  result = av_bsf_init(guard.context);
  if (result < 0) throw std::runtime_error("av_bsf_init failed: " + av_error_to_string(result));
  return guard;
}

}  // namespace

void probe_media(const ProbeOptions& options) {
  FormatContextGuard guard;
  int result = avformat_open_input(&guard.context, options.input.c_str(), nullptr, nullptr);
  if (result < 0) throw std::runtime_error("avformat_open_input failed: " + av_error_to_string(result));

  result = avformat_find_stream_info(guard.context, nullptr);
  if (result < 0) throw std::runtime_error("avformat_find_stream_info failed: " + av_error_to_string(result));

  print_stream_info(guard.context);

  const int video_stream_index = find_video_stream(guard.context);
  if (video_stream_index < 0) {
    std::cout << "no video stream, packet GOP analysis skipped\n";
    return;
  }

  const AVStream* video_stream = guard.context->streams[video_stream_index];
  const AVCodecParameters* video_params = video_stream->codecpar;
  const bool enable_annexb = !options.annexb_output.empty() && video_params->codec_id == AV_CODEC_ID_H264;
  BsfGuard bsf;
  std::ofstream annexb_output;
  NaluStats nalu_stats;
  if (enable_annexb) {
    bsf = create_h264_mp4toannexb_filter(video_params);
    annexb_output.open(options.annexb_output, std::ios::binary);
    if (!annexb_output) throw std::runtime_error("failed to open annexb output: " + options.annexb_output);
  }

  PacketGuard input_packet;
  PacketGuard filtered_packet;
  if (!input_packet.packet || !filtered_packet.packet) throw std::runtime_error("av_packet_alloc failed");

  std::cout << "video packet probe, stream #" << video_stream_index << "\n";
  std::cout << "index, pts, dts, duration, size, keyframe, gop_delta\n";

  int printed = 0;
  int last_key_packet_index = -1;
  int gop_count = 0;
  int min_gop = 0;
  int max_gop = 0;
  int total_gop = 0;

  while (printed < options.packets && av_read_frame(guard.context, input_packet.packet) >= 0) {
    AVPacket* packet = input_packet.packet;
    if (packet->stream_index != video_stream_index) {
      av_packet_unref(packet);
      continue;
    }

    const bool keyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0;
    int gop_delta = -1;
    if (keyframe) {
      if (last_key_packet_index >= 0) {
        gop_delta = printed - last_key_packet_index;
        min_gop = gop_count == 0 ? gop_delta : std::min(min_gop, gop_delta);
        max_gop = gop_count == 0 ? gop_delta : std::max(max_gop, gop_delta);
        total_gop += gop_delta;
        gop_count += 1;
      }
      last_key_packet_index = printed;
    }

    std::cout << printed << ", "
              << timestamp_to_text(packet->pts, video_stream->time_base) << ", "
              << timestamp_to_text(packet->dts, video_stream->time_base) << ", "
              << timestamp_to_text(packet->duration, video_stream->time_base) << ", "
              << packet->size << ", "
              << (keyframe ? "yes" : "no") << ", "
              << (gop_delta >= 0 ? std::to_string(gop_delta) : "-") << "\n";

    if (enable_annexb) {
      result = av_bsf_send_packet(bsf.context, packet);
      if (result < 0) throw std::runtime_error("av_bsf_send_packet failed: " + av_error_to_string(result));
      while ((result = av_bsf_receive_packet(bsf.context, filtered_packet.packet)) == 0) {
        annexb_output.write(reinterpret_cast<const char*>(filtered_packet.packet->data), filtered_packet.packet->size);
        collect_nalu_stats(filtered_packet.packet->data, filtered_packet.packet->size, nalu_stats);
        av_packet_unref(filtered_packet.packet);
      }
      if (result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
        throw std::runtime_error("av_bsf_receive_packet failed: " + av_error_to_string(result));
      }
    }

    printed += 1;
    av_packet_unref(packet);
  }

  if (enable_annexb) {
    av_bsf_send_packet(bsf.context, nullptr);
    while (av_bsf_receive_packet(bsf.context, filtered_packet.packet) == 0) {
      annexb_output.write(reinterpret_cast<const char*>(filtered_packet.packet->data), filtered_packet.packet->size);
      collect_nalu_stats(filtered_packet.packet->data, filtered_packet.packet->size, nalu_stats);
      av_packet_unref(filtered_packet.packet);
    }
  }

  std::cout << "\nsummary:\n";
  std::cout << "  printed_video_packets : " << printed << "\n";
  std::cout << "  keyframe_intervals    : " << gop_count << "\n";
  if (gop_count > 0) {
    std::cout << "  gop_min_packets       : " << min_gop << "\n";
    std::cout << "  gop_max_packets       : " << max_gop << "\n";
    std::cout << "  gop_avg_packets       : " << std::fixed << std::setprecision(2)
              << static_cast<double>(total_gop) / gop_count << "\n";
  }

  if (enable_annexb) {
    print_nalu_stats(nalu_stats);
    std::cout << "  annexb_output: " << options.annexb_output << "\n";
  }
}


