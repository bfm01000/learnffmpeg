#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace {

struct Options {
  std::string input;
  std::string output = "/tmp/frxxz-preview.i420";
  std::string csv = "/tmp/frxxz-preview-frames.csv";
  int frames = 30;
  int width = 0;
  int height = 0;
};

struct FormatContextGuard {
  AVFormatContext* context = nullptr;
  ~FormatContextGuard() {
    if (context) avformat_close_input(&context);
  }
};

struct CodecContextGuard {
  AVCodecContext* context = nullptr;
  ~CodecContextGuard() {
    if (context) avcodec_free_context(&context);
  }
};

struct PacketGuard {
  AVPacket* packet = av_packet_alloc();
  ~PacketGuard() {
    if (packet) av_packet_free(&packet);
  }
};

struct FrameGuard {
  AVFrame* frame = av_frame_alloc();
  ~FrameGuard() {
    if (frame) av_frame_free(&frame);
  }
};

struct SwsContextGuard {
  SwsContext* context = nullptr;
  ~SwsContextGuard() {
    if (context) sws_freeContext(context);
  }
};

std::string av_error_to_string(int error) {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(error, buffer, sizeof(buffer));
  return std::string(buffer);
}

void print_usage(const char* argv0) {
  std::cout << "Usage:\n"
            << "  " << argv0 << " --input FRXXZ.mp4 [--frames 30] [--output out.i420] [--csv frames.csv]\n"
            << "  " << argv0 << " --input FRXXZ.mp4 --frames 60 --width 640 --height 360\n";
}

int parse_positive_int(const std::string& text, const char* name) {
  int value = std::stoi(text);
  if (value <= 0) throw std::runtime_error(std::string(name) + " must be positive");
  return value;
}

Options parse_args(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto read_value = [&](const char* name) -> std::string {
      const std::string prefix = std::string(name) + "=";
      if (arg.rfind(prefix, 0) == 0) return arg.substr(prefix.size());
      if (arg == name && i + 1 < argc) return argv[++i];
      return "";
    };

    std::string value;
    if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      std::exit(0);
    } else if (!(value = read_value("--input")).empty()) {
      options.input = value;
    } else if (!(value = read_value("--output")).empty()) {
      options.output = value;
    } else if (!(value = read_value("--csv")).empty()) {
      options.csv = value;
    } else if (!(value = read_value("--frames")).empty()) {
      options.frames = parse_positive_int(value, "--frames");
    } else if (!(value = read_value("--width")).empty()) {
      options.width = parse_positive_int(value, "--width");
    } else if (!(value = read_value("--height")).empty()) {
      options.height = parse_positive_int(value, "--height");
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }

  if (options.input.empty()) throw std::runtime_error("--input is required");
  if ((options.width == 0) != (options.height == 0)) {
    throw std::runtime_error("--width and --height must be provided together");
  }
  return options;
}

int find_video_stream(const AVFormatContext* format_context) {
  for (unsigned int i = 0; i < format_context->nb_streams; ++i) {
    if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

double timestamp_to_seconds(std::int64_t timestamp, AVRational time_base) {
  if (timestamp == AV_NOPTS_VALUE || time_base.den == 0) return 0.0;
  return static_cast<double>(timestamp) * static_cast<double>(time_base.num) / static_cast<double>(time_base.den);
}

void decode_video_file(const Options& options) {
  FormatContextGuard format;
  int result = avformat_open_input(&format.context, options.input.c_str(), nullptr, nullptr);
  if (result < 0) throw std::runtime_error("avformat_open_input failed: " + av_error_to_string(result));

  result = avformat_find_stream_info(format.context, nullptr);
  if (result < 0) throw std::runtime_error("avformat_find_stream_info failed: " + av_error_to_string(result));

  const int video_stream_index = find_video_stream(format.context);
  if (video_stream_index < 0) throw std::runtime_error("no video stream found");

  AVStream* video_stream = format.context->streams[video_stream_index];
  const AVCodecParameters* params = video_stream->codecpar;
  const AVCodec* codec = avcodec_find_decoder(params->codec_id);
  if (!codec) throw std::runtime_error("decoder not found for codec: " + std::string(avcodec_get_name(params->codec_id)));

  CodecContextGuard codec_context;
  codec_context.context = avcodec_alloc_context3(codec);
  if (!codec_context.context) throw std::runtime_error("avcodec_alloc_context3 failed");

  result = avcodec_parameters_to_context(codec_context.context, params);
  if (result < 0) throw std::runtime_error("avcodec_parameters_to_context failed: " + av_error_to_string(result));

  result = avcodec_open2(codec_context.context, codec, nullptr);
  if (result < 0) throw std::runtime_error("avcodec_open2 failed: " + av_error_to_string(result));

  const int dst_width = options.width > 0 ? options.width : codec_context.context->width;
  const int dst_height = options.height > 0 ? options.height : codec_context.context->height;
  const AVPixelFormat dst_format = AV_PIX_FMT_YUV420P;

  SwsContextGuard sws;
  sws.context = sws_getContext(codec_context.context->width,
                               codec_context.context->height,
                               codec_context.context->pix_fmt,
                               dst_width,
                               dst_height,
                               dst_format,
                               SWS_BILINEAR,
                               nullptr,
                               nullptr,
                               nullptr);
  if (!sws.context) throw std::runtime_error("sws_getContext failed");

  const int buffer_size = av_image_get_buffer_size(dst_format, dst_width, dst_height, 1);
  if (buffer_size <= 0) throw std::runtime_error("av_image_get_buffer_size failed");
  std::vector<std::uint8_t> i420_buffer(static_cast<std::size_t>(buffer_size));
  std::uint8_t* dst_data[4] = {};
  int dst_linesize[4] = {};
  result = av_image_fill_arrays(dst_data, dst_linesize, i420_buffer.data(), dst_format, dst_width, dst_height, 1);
  if (result < 0) throw std::runtime_error("av_image_fill_arrays failed: " + av_error_to_string(result));

  std::ofstream yuv_output(options.output, std::ios::binary);
  if (!yuv_output) throw std::runtime_error("failed to open output: " + options.output);
  std::ofstream csv_output(options.csv);
  if (!csv_output) throw std::runtime_error("failed to open csv: " + options.csv);
  csv_output << "index,pts_seconds,width,height,pix_fmt,bytes\n";

  PacketGuard packet;
  FrameGuard frame;
  if (!packet.packet || !frame.frame) throw std::runtime_error("failed to allocate packet/frame");
  int decoded_total = 0;
  while (decoded_total < options.frames && av_read_frame(format.context, packet.packet) >= 0) {
    if (packet.packet->stream_index != video_stream_index) {
      av_packet_unref(packet.packet);
      continue;
    }

    result = avcodec_send_packet(codec_context.context, packet.packet);
    av_packet_unref(packet.packet);
    if (result < 0) throw std::runtime_error("avcodec_send_packet failed: " + av_error_to_string(result));

    while (decoded_total < options.frames) {
      const int receive = avcodec_receive_frame(codec_context.context, frame.frame);
      if (receive == AVERROR(EAGAIN) || receive == AVERROR_EOF) break;
      if (receive < 0) throw std::runtime_error("avcodec_receive_frame failed: " + av_error_to_string(receive));

      sws_scale(sws.context, frame.frame->data, frame.frame->linesize, 0, codec_context.context->height, dst_data, dst_linesize);
      yuv_output.write(reinterpret_cast<const char*>(i420_buffer.data()), i420_buffer.size());
      const double pts_seconds = timestamp_to_seconds(frame.frame->best_effort_timestamp, video_stream->time_base);
      csv_output << decoded_total << "," << std::fixed << std::setprecision(6) << pts_seconds << ","
                 << dst_width << "," << dst_height << ",i420," << i420_buffer.size() << "\n";
      av_frame_unref(frame.frame);
      decoded_total += 1;
    }
  }

  if (decoded_total < options.frames) {
    avcodec_send_packet(codec_context.context, nullptr);
    while (decoded_total < options.frames) {
      const int receive = avcodec_receive_frame(codec_context.context, frame.frame);
      if (receive == AVERROR(EAGAIN) || receive == AVERROR_EOF) break;
      if (receive < 0) throw std::runtime_error("avcodec_receive_frame failed: " + av_error_to_string(receive));

      sws_scale(sws.context, frame.frame->data, frame.frame->linesize, 0, codec_context.context->height, dst_data, dst_linesize);
      yuv_output.write(reinterpret_cast<const char*>(i420_buffer.data()), i420_buffer.size());
      const double pts_seconds = timestamp_to_seconds(frame.frame->best_effort_timestamp, video_stream->time_base);
      csv_output << decoded_total << "," << std::fixed << std::setprecision(6) << pts_seconds << ","
                 << dst_width << "," << dst_height << ",i420," << i420_buffer.size() << "\n";
      av_frame_unref(frame.frame);
      decoded_total += 1;
    }
  }

  std::cout << "input      : " << options.input << "\n"
            << "codec      : " << avcodec_get_name(params->codec_id) << "\n"
            << "source     : " << codec_context.context->width << "x" << codec_context.context->height
            << " pix_fmt=" << av_get_pix_fmt_name(codec_context.context->pix_fmt) << "\n"
            << "output     : " << options.output << "\n"
            << "csv        : " << options.csv << "\n"
            << "i420       : " << dst_width << "x" << dst_height << ", " << buffer_size << " bytes/frame\n"
            << "frames     : " << decoded_total << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    decode_video_file(parse_args(argc, argv));
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
}
