#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

// 全局运行参数。该结构体和 CLI 参数一一对应，
// 尽量保持“读到参数名就知道用途”。
struct Options {
  std::string input_path;           // 输入媒体文件路径（必填），例如 input.mkv
  std::string output_path;          // 输出文件路径（必填），目标通常是 output.mp4
  int crf = 23;                     // 视频质量因子（仅重编码模式生效）：越小质量越高、体积越大
  std::string preset = "medium";    // x264 编码速度档位（仅重编码模式）：越慢压缩率越高
  std::string audio_bitrate = "128k"; // 音频目标码率（仅重编码模式），如 128k / 192k
  bool copy_mode = false;           // 是否启用直拷贝模式（true=不重编码，只重封装）
  // video_index / audio_index 的必要性：
  // 1) 多轨文件里“第一路”不一定是你想要的主轨（流顺序可能因封装工具不同而变化）
  // 2) 批处理场景需要结果可复现，不能每次依赖“默认第一路”
  // 3) 某些文件缺少 language metadata，audio_lang 无法命中时只能靠显式索引
  int video_index = -1;             // 指定输入视频流索引；常用于多机位/主视频+预览视频场景
  int audio_index = -1;             // 指定输入音频流索引；常用于多语言/立体声与5.1/评论音轨场景
  std::string audio_lang;           // 按语言标签选音轨（如 eng/chi），未命中则回退第一路音频
};

// 每一路参与处理的流（最多视频一路 + 音频一路）都对应一个上下文。
// 这里集中保存该流的解码器、编码器和中间状态（如下一帧 pts）。
struct StreamContext {
  AVMediaType type = AVMEDIA_TYPE_UNKNOWN; // 流类型：视频流或音频流（决定后续走哪条处理逻辑）
  int input_index = -1;                    // 输入容器中的流索引（来自 ifmt_ctx->streams[]）
  int output_index = -1;                   // 输出容器中的流索引（对应 ofmt_ctx->streams[]）

  AVCodecContext *dec_ctx = nullptr;       // 解码器上下文：把压缩 packet 解成原始 frame
  AVCodecContext *enc_ctx = nullptr;       // 编码器上下文：把原始 frame 编回目标编码 packet

  SwsContext *sws_ctx = nullptr;           // 视频像素格式/尺寸转换上下文（例如转为 yuv420p）
  SwrContext *swr_ctx = nullptr;           // 音频重采样上下文（采样率/采样格式/声道布局转换）
  AVAudioFifo *audio_fifo = nullptr;       // 音频 FIFO 缓冲：平滑“解码帧大小”和“编码器帧大小”的差异

  int64_t next_video_pts = 0;              // 视频兜底时间戳：源流无 pts 时按递增生成，保证单调
  int64_t next_audio_pts = 0;              // 音频输出时间戳游标：按样本数推进（time_base=1/sample_rate）
};

static std::string ff_err2str(int err) {
  char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
  av_strerror(err, buf, sizeof(buf));
  return std::string(buf);
}

enum class LogLevel { kInfo, kWarn, kError };

static const char *log_prefix(LogLevel level) {
  switch (level) {
    case LogLevel::kInfo:
      return "[INFO] ";
    case LogLevel::kWarn:
      return "[WARN] ";
    case LogLevel::kError:
      return "[ERROR] ";
  }
  return "";
}

static void log_msg(LogLevel level, const std::string &msg) {
  std::cerr << log_prefix(level) << msg << "\n";
}

enum ExitCode {
  EXIT_OK = 0,
  EXIT_ARG_ERROR = 101,
  EXIT_INPUT_OPEN_ERROR = 201,
  EXIT_INPUT_PROBE_ERROR = 202,
  EXIT_STREAM_SELECT_ERROR = 203,
  EXIT_OUTPUT_CREATE_ERROR = 204,
  EXIT_DECODE_ERROR = 301,
  EXIT_ENCODE_ERROR = 401,
  EXIT_MUX_ERROR = 501,
  EXIT_RUNTIME_ERROR = 601,
};

static void print_usage() {
  std::cout
      << "Usage:\n"
      << "  transcoder --input <input> --output <output.mp4> [options]\n\n"
      << "Options:\n"
      << "  --crf <int>               Video CRF (default: 23)\n"
      << "  --preset <name>           x264 preset (default: medium)\n"
      << "  --audio-bitrate <value>   Audio bitrate (default: 128k)\n"
      << "  --video-index <int>       Select input video stream index\n"
      << "  --audio-index <int>       Select input audio stream index\n"
      << "  --audio-lang <code>       Select audio stream by language tag (e.g. eng)\n"
      << "  --copy                    Stream copy mode (no re-encode)\n"
      << "  --help                    Show this help\n";
}

static bool parse_args(int argc, char **argv, Options &opt) {
  // 简单参数解析器：教学优先，不引入额外依赖。
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto need_value = [&](const char *name) -> const char * {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for " << name << "\n";
        std::exit(101);
      }
      return argv[++i];
    };

    if (arg == "--input") {
      opt.input_path = need_value("--input");
    } else if (arg == "--output") {
      opt.output_path = need_value("--output");
    } else if (arg == "--crf") {
      opt.crf = std::atoi(need_value("--crf"));
    } else if (arg == "--preset") {
      opt.preset = need_value("--preset");
    } else if (arg == "--audio-bitrate") {
      opt.audio_bitrate = need_value("--audio-bitrate");
    } else if (arg == "--video-index") {
      opt.video_index = std::atoi(need_value("--video-index"));
    } else if (arg == "--audio-index") {
      opt.audio_index = std::atoi(need_value("--audio-index"));
    } else if (arg == "--audio-lang") {
      opt.audio_lang = need_value("--audio-lang");
    } else if (arg == "--copy") {
      opt.copy_mode = true;
    } else if (arg == "--help") {
      print_usage();
      return false;
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      print_usage();
      return false;
    }
  }

  if (opt.input_path.empty() || opt.output_path.empty()) {
    std::cerr << "--input and --output are required.\n";
    print_usage();
    return false;
  }
  return true;
}

static int parse_bitrate(const std::string &value) {
  // 支持 "128k" / "1m" / "128000" 三种常见写法。
  if (value.empty()) {
    return 128000;
  }
  const char suffix = value.back();
  if (suffix == 'k' || suffix == 'K') {
    return std::atoi(value.substr(0, value.size() - 1).c_str()) * 1000;
  }
  if (suffix == 'm' || suffix == 'M') {
    return std::atoi(value.substr(0, value.size() - 1).c_str()) * 1000 * 1000;
  }
  return std::atoi(value.c_str());
}

static int select_first_stream(AVFormatContext *ifmt_ctx, AVMediaType type) {
  for (unsigned int i = 0; i < ifmt_ctx->nb_streams; ++i) {
    if (ifmt_ctx->streams[i]->codecpar->codec_type == type) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

static std::string lowercase(std::string s) {
  for (char &c : s) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return s;
}

static int select_audio_stream(AVFormatContext *ifmt_ctx, const Options &opt) {
  // 优先级：
  // 1) --audio-index 明确指定
  // 2) --audio-lang 按 metadata.language 选择
  // 3) 回退到第一路音频
  if (opt.audio_index >= 0) {
    return opt.audio_index;
  }
  if (opt.audio_lang.empty()) {
    return select_first_stream(ifmt_ctx, AVMEDIA_TYPE_AUDIO);
  }
  const std::string target = lowercase(opt.audio_lang);
  for (unsigned int i = 0; i < ifmt_ctx->nb_streams; ++i) {
    AVStream *st = ifmt_ctx->streams[i];
    if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
      continue;
    }
    AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", nullptr, 0);
    if (!lang || !lang->value) {
      continue;
    }
    if (lowercase(lang->value) == target) {
      return static_cast<int>(i);
    }
  }
  log_msg(LogLevel::kWarn,
          "No audio stream matched --audio-lang=" + opt.audio_lang +
              ", fallback to first audio stream.");
  return select_first_stream(ifmt_ctx, AVMEDIA_TYPE_AUDIO);
}

static int open_decoder(AVFormatContext *ifmt_ctx, StreamContext &sc) {
  // 解码器的 codec_id 来自输入流 codecpar。
  AVStream *in_stream = ifmt_ctx->streams[sc.input_index];
  const AVCodec *decoder = avcodec_find_decoder(in_stream->codecpar->codec_id);
  if (!decoder) {
    std::cerr << "No decoder for stream " << sc.input_index << "\n";
    return AVERROR_DECODER_NOT_FOUND;
  }

  sc.dec_ctx = avcodec_alloc_context3(decoder);
  if (!sc.dec_ctx) {
    return AVERROR(ENOMEM);
  }
  int ret = avcodec_parameters_to_context(sc.dec_ctx, in_stream->codecpar);
  if (ret < 0) {
    return ret;
  }
  ret = avcodec_open2(sc.dec_ctx, decoder, nullptr);
  if (ret < 0) {
    std::cerr << "avcodec_open2 decoder failed: " << ff_err2str(ret) << "\n";
    return ret;
  }
  return 0;
}

static AVPixelFormat pick_pixel_format() {
  // 选 yuv420p 是为了最大兼容性（播放器/硬件解码普遍支持）。
  return AV_PIX_FMT_YUV420P;
}

static AVSampleFormat pick_sample_format() {
  // AAC 常用浮点平面格式，作为默认教学配置。
  return AV_SAMPLE_FMT_FLTP;
}

static AVChannelLayout infer_channel_layout(const AVCodecContext *ctx) {
  // 如果输入流里有有效布局就沿用，否则回退为双声道。
  AVChannelLayout layout;
  if (ctx->ch_layout.nb_channels > 0) {
    av_channel_layout_copy(&layout, &ctx->ch_layout);
  } else {
    av_channel_layout_default(&layout, 2);
  }
  return layout;
}

static int open_video_encoder(const Options &opt, AVFormatContext *ifmt_ctx,
                              AVFormatContext *ofmt_ctx, StreamContext &sc) {
  // 优先使用 libx264；若环境未启用则回退 FFmpeg 内建 H264 encoder。
  const AVCodec *encoder = avcodec_find_encoder_by_name("libx264");
  if (!encoder) {
    encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
  }
  if (!encoder) {
    return AVERROR_ENCODER_NOT_FOUND;
  }

  AVStream *in_stream = ifmt_ctx->streams[sc.input_index];
  sc.enc_ctx = avcodec_alloc_context3(encoder);
  if (!sc.enc_ctx) {
    return AVERROR(ENOMEM);
  }

  sc.enc_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
  sc.enc_ctx->width = sc.dec_ctx->width;
  sc.enc_ctx->height = sc.dec_ctx->height;
  sc.enc_ctx->sample_aspect_ratio = sc.dec_ctx->sample_aspect_ratio;
  sc.enc_ctx->pix_fmt = pick_pixel_format();

  // 用输入帧率推导 encoder time_base。
  // 若取不到帧率，回退 25fps 避免 time_base 非法。
  AVRational fr = av_guess_frame_rate(ifmt_ctx, in_stream, nullptr);
  if (fr.num <= 0 || fr.den <= 0) {
    fr = AVRational{25, 1};
  }
  sc.enc_ctx->time_base = av_inv_q(fr);
  sc.enc_ctx->framerate = fr;
  sc.enc_ctx->gop_size = 50;
  sc.enc_ctx->max_b_frames = 2;

  // 设置编码器的 preset (预设) 参数，用于平衡编码速度和压缩率（如 ultrafast, medium, slow 等）。越慢压缩率越高。
  av_opt_set(sc.enc_ctx->priv_data, "preset", opt.preset.c_str(), 0);
  // 设置 CRF (Constant Rate Factor，恒定质量因子)，用于控制输出视频的画质。数值越小画质越好，文件越大（x264 默认 23，推荐范围 18-28）。
  av_opt_set_int(sc.enc_ctx->priv_data, "crf", opt.crf, 0);

  // 如果输出封装格式（如 MP4、FLV）要求全局头信息（即 SPS/PPS 等配置信息放在文件头部，而不是每个关键帧前面），
  // 则告诉编码器生成全局头信息。
  if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
    sc.enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }

  int ret = avcodec_open2(sc.enc_ctx, encoder, nullptr);
  if (ret < 0) {
    return ret;
  }

  AVStream *out_stream = avformat_new_stream(ofmt_ctx, nullptr);
  if (!out_stream) {
    return AVERROR(ENOMEM);
  }
  sc.output_index = out_stream->index;
  out_stream->time_base = sc.enc_ctx->time_base;

  ret = avcodec_parameters_from_context(out_stream->codecpar, sc.enc_ctx);
  if (ret < 0) {
    return ret;
  }
  out_stream->codecpar->codec_tag = 0;
  return 0;
}

static int open_audio_encoder(const Options &opt, AVFormatContext *ofmt_ctx, StreamContext &sc) {
  const AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
  if (!encoder) {
    return AVERROR_ENCODER_NOT_FOUND;
  }

  sc.enc_ctx = avcodec_alloc_context3(encoder);
  if (!sc.enc_ctx) {
    return AVERROR(ENOMEM);
  }

  AVChannelLayout out_ch_layout = infer_channel_layout(sc.dec_ctx);
  const int out_sample_rate = sc.dec_ctx->sample_rate > 0 ? sc.dec_ctx->sample_rate : 48000;

  sc.enc_ctx->codec_type = AVMEDIA_TYPE_AUDIO;
  sc.enc_ctx->sample_rate = out_sample_rate;
  av_channel_layout_copy(&sc.enc_ctx->ch_layout, &out_ch_layout);
  av_channel_layout_uninit(&out_ch_layout);
  sc.enc_ctx->sample_fmt = pick_sample_format();
  sc.enc_ctx->bit_rate = parse_bitrate(opt.audio_bitrate);
  sc.enc_ctx->time_base = AVRational{1, sc.enc_ctx->sample_rate};

  // 如果输出封装格式（如 MP4、FLV）要求全局头信息（即 SPS/PPS 等配置信息放在文件头部，而不是每个关键帧前面），
  // 则告诉编码器生成全局头信息。
  if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
    sc.enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }

  int ret = avcodec_open2(sc.enc_ctx, encoder, nullptr);
  if (ret < 0) {
    return ret;
  }

  AVStream *out_stream = avformat_new_stream(ofmt_ctx, nullptr);
  if (!out_stream) {
    return AVERROR(ENOMEM);
  }
  sc.output_index = out_stream->index;
  out_stream->time_base = sc.enc_ctx->time_base;

  ret = avcodec_parameters_from_context(out_stream->codecpar, sc.enc_ctx);
  if (ret < 0) {
    return ret;
  }
  out_stream->codecpar->codec_tag = 0;

  // swr 负责把输入音频转换成编码器需要的格式：
  // 采样格式 / 采样率 / 声道布局 三者都在这里统一。
  AVChannelLayout in_layout = infer_channel_layout(sc.dec_ctx);
  ret = swr_alloc_set_opts2(
      &sc.swr_ctx,
      &sc.enc_ctx->ch_layout,
      sc.enc_ctx->sample_fmt,
      sc.enc_ctx->sample_rate,
      &in_layout,
      sc.dec_ctx->sample_fmt,
      sc.dec_ctx->sample_rate > 0 ? sc.dec_ctx->sample_rate : sc.enc_ctx->sample_rate,
      0,
      nullptr);
  av_channel_layout_uninit(&in_layout);
  if (ret < 0 || !sc.swr_ctx) {
    return ret < 0 ? ret : AVERROR(ENOMEM);
  }
  ret = swr_init(sc.swr_ctx);
  if (ret < 0) {
    return ret;
  }

  // 音频 FIFO 用来平滑“解码输出帧大小”和“编码器要求帧大小”之间的不匹配。
  sc.audio_fifo = av_audio_fifo_alloc(sc.enc_ctx->sample_fmt, sc.enc_ctx->ch_layout.nb_channels, 1);
  if (!sc.audio_fifo) {
    return AVERROR(ENOMEM);
  }

  return 0;
}

static int write_encoded_packets(StreamContext &sc, AVFormatContext *ofmt_ctx) {
  AVPacket *pkt = av_packet_alloc();
  if (!pkt) {
    return AVERROR(ENOMEM);
  }

  int ret = 0;
  while ((ret = avcodec_receive_packet(sc.enc_ctx, pkt)) >= 0) {
    AVStream *out_stream = ofmt_ctx->streams[sc.output_index];
    // 编码器 packet 的时间基是 enc_ctx->time_base，
    // 复用器要求的是 out_stream->time_base，写入前必须 rescale。
    av_packet_rescale_ts(pkt, sc.enc_ctx->time_base, out_stream->time_base);
    pkt->stream_index = sc.output_index;
    ret = av_interleaved_write_frame(ofmt_ctx, pkt);
    av_packet_unref(pkt);
    if (ret < 0) {
      av_packet_free(&pkt);
      return ret;
    }
  }

  av_packet_free(&pkt);
  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
    return 0;
  }
  return ret;
}

static int encode_video_frame(StreamContext &sc, AVFrame *decoded, AVFormatContext *ifmt_ctx,
                              AVFormatContext *ofmt_ctx) {
  AVFrame *frame = av_frame_alloc();
  if (!frame) {
    return AVERROR(ENOMEM);
  }
  frame->format = sc.enc_ctx->pix_fmt;
  frame->width = sc.enc_ctx->width;
  frame->height = sc.enc_ctx->height;

  int ret = av_frame_get_buffer(frame, 0);
  if (ret < 0) {
    av_frame_free(&frame);
    return ret;
  }

  // 使用 swscale 统一输入像素格式到编码器像素格式（默认 yuv420p）。
  sc.sws_ctx = sws_getCachedContext(
      sc.sws_ctx,
      sc.dec_ctx->width,
      sc.dec_ctx->height,
      sc.dec_ctx->pix_fmt,
      sc.enc_ctx->width,
      sc.enc_ctx->height,
      sc.enc_ctx->pix_fmt,
      SWS_BILINEAR,
      nullptr,
      nullptr,
      nullptr);
  if (!sc.sws_ctx) {
    av_frame_free(&frame);
    return AVERROR(EINVAL);
  }

  sws_scale(sc.sws_ctx,
            decoded->data,
            decoded->linesize,
            0,
            sc.dec_ctx->height,
            frame->data,
            frame->linesize);

  int64_t src_pts = decoded->best_effort_timestamp;
  if (src_pts == AV_NOPTS_VALUE) {
    src_pts = decoded->pts;
  }
  if (src_pts != AV_NOPTS_VALUE) {
    AVStream *in_stream = ifmt_ctx->streams[sc.input_index];
    // 关键：输入帧 pts 在输入流 time_base，下游编码器使用 enc_ctx->time_base。
    frame->pts = av_rescale_q(src_pts, in_stream->time_base, sc.enc_ctx->time_base);
    sc.next_video_pts = frame->pts + 1;
  } else {
    // 如果源流没有可用时间戳，退化为线性递增，确保输出单调。
    frame->pts = sc.next_video_pts++;
  }

  ret = avcodec_send_frame(sc.enc_ctx, frame);
  av_frame_free(&frame);
  if (ret < 0) {
    return ret;
  }
  return write_encoded_packets(sc, ofmt_ctx);
}

static int fifo_write_audio(StreamContext &sc, AVFrame *decoded) {
  // swr_get_delay + 当前 nb_samples 用于估算重采样后的目标采样数，
  // 避免目标缓存分配不足。
  const int64_t delay = swr_get_delay(sc.swr_ctx, sc.dec_ctx->sample_rate);
  const int dst_nb_samples = av_rescale_rnd(
      delay + decoded->nb_samples,
      sc.enc_ctx->sample_rate,
      sc.dec_ctx->sample_rate,
      AV_ROUND_UP);

  uint8_t **converted_data = nullptr;
  int ret = av_samples_alloc_array_and_samples(
      &converted_data,
      nullptr,
      sc.enc_ctx->ch_layout.nb_channels,
      dst_nb_samples,
      sc.enc_ctx->sample_fmt,
      0);
  if (ret < 0) {
    return ret;
  }

  // 将解码出的音频转换成编码器目标格式，再写入 FIFO。
  ret = swr_convert(
      sc.swr_ctx,
      converted_data,
      dst_nb_samples,
      const_cast<const uint8_t **>(decoded->extended_data),
      decoded->nb_samples);
  if (ret < 0) {
    av_freep(&converted_data[0]);
    av_freep(&converted_data);
    return ret;
  }

  const int converted_samples = ret;
  if (av_audio_fifo_realloc(sc.audio_fifo, av_audio_fifo_size(sc.audio_fifo) + converted_samples) < 0) {
    av_freep(&converted_data[0]);
    av_freep(&converted_data);
    return AVERROR(ENOMEM);
  }

  const int wrote = av_audio_fifo_write(sc.audio_fifo, reinterpret_cast<void **>(converted_data), converted_samples);
  av_freep(&converted_data[0]);
  av_freep(&converted_data);
  if (wrote < converted_samples) {
    return AVERROR(EIO);
  }
  return 0;
}

static int encode_audio_from_fifo(StreamContext &sc, AVFormatContext *ofmt_ctx, bool flush) {
  const bool variable_frame_size =
      (sc.enc_ctx->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) != 0;
  const int frame_size = sc.enc_ctx->frame_size > 0 ? sc.enc_ctx->frame_size : 1024;

  while (av_audio_fifo_size(sc.audio_fifo) >= frame_size ||
         (flush && av_audio_fifo_size(sc.audio_fifo) > 0)) {
    const int fifo_size = av_audio_fifo_size(sc.audio_fifo);
    const int read_samples = variable_frame_size ? fifo_size : frame_size;
    const int alloc_samples = variable_frame_size ? read_samples : frame_size;

    AVFrame *frame = av_frame_alloc();
    if (!frame) {
      return AVERROR(ENOMEM);
    }
    frame->nb_samples = alloc_samples;
    frame->format = sc.enc_ctx->sample_fmt;
    av_channel_layout_copy(&frame->ch_layout, &sc.enc_ctx->ch_layout);
    frame->sample_rate = sc.enc_ctx->sample_rate;

    int ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
      av_frame_free(&frame);
      return ret;
    }

    const int actual_read = av_audio_fifo_read(sc.audio_fifo, reinterpret_cast<void **>(frame->data), read_samples);
    if (actual_read < 0) {
      av_frame_free(&frame);
      return AVERROR(EIO);
    }
    // 固定帧长编码器（如常见 AAC）要求每帧 frame_size 个样本；
    // flush 尾声时不足 frame_size 需要补静音。
    if (!variable_frame_size && actual_read < frame_size) {
      av_samples_set_silence(
          frame->data,
          actual_read,
          frame_size - actual_read,
          sc.enc_ctx->ch_layout.nb_channels,
          sc.enc_ctx->sample_fmt);
    }

    frame->pts = sc.next_audio_pts;
    // 音频时间轴以“采样点”为单位推进，time_base=1/sample_rate。
    sc.next_audio_pts += frame->nb_samples;

    ret = avcodec_send_frame(sc.enc_ctx, frame);
    av_frame_free(&frame);
    if (ret < 0) {
      return ret;
    }
    ret = write_encoded_packets(sc, ofmt_ctx);
    if (ret < 0) {
      return ret;
    }
  }

  if (flush) {
    int ret = avcodec_send_frame(sc.enc_ctx, nullptr);
    if (ret < 0) {
      return ret;
    }
    return write_encoded_packets(sc, ofmt_ctx);
  }
  return 0;
}

static int64_t packet_pts_to_us(const AVPacket *pkt, const AVStream *stream) {
  if (!pkt || !stream || pkt->pts == AV_NOPTS_VALUE) {
    return AV_NOPTS_VALUE;
  }
  return av_rescale_q(pkt->pts, stream->time_base, AV_TIME_BASE_Q);
}

static std::string format_seconds(double seconds) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1) << seconds << "s";
  return oss.str();
}

static void maybe_print_progress(const char *prefix, int64_t current_us, int64_t duration_us,
                                 int64_t &last_print_us) {
  if (duration_us <= 0 || current_us == AV_NOPTS_VALUE) {
    return;
  }
  // 约每 0.5 秒刷一次，避免终端输出过于频繁。
  if (current_us - last_print_us < 500000) {
    return;
  }
  const double ratio = std::max(0.0, std::min(1.0, static_cast<double>(current_us) / duration_us));
  const double current_s = static_cast<double>(current_us) / AV_TIME_BASE;
  const double total_s = static_cast<double>(duration_us) / AV_TIME_BASE;
  std::cout << "\r" << prefix << " " << std::fixed << std::setprecision(1) << (ratio * 100.0) << "%"
            << " (" << format_seconds(current_s) << "/" << format_seconds(total_s) << ")"
            << std::flush;
  last_print_us = current_us;
}

static int copy_mode_run(AVFormatContext *ifmt_ctx, AVFormatContext *ofmt_ctx,
                         const std::unordered_map<int, int> &in_to_out,
                         int64_t input_duration_us) {
  AVPacket *pkt = av_packet_alloc();
  if (!pkt) {
    return AVERROR(ENOMEM);
  }

  int64_t last_progress_us = -500000;
  int ret = 0;
  while ((ret = av_read_frame(ifmt_ctx, pkt)) >= 0) {
    auto it = in_to_out.find(pkt->stream_index);
    if (it == in_to_out.end()) {
      av_packet_unref(pkt);
      continue;
    }
    const int out_index = it->second;
    AVStream *in_stream = ifmt_ctx->streams[pkt->stream_index];
    AVStream *out_stream = ofmt_ctx->streams[out_index];
    maybe_print_progress("Copy progress:", packet_pts_to_us(pkt, in_stream), input_duration_us, last_progress_us);

    // 直拷贝模式下仅做时间戳换算和重封装，不做解码/编码。
    av_packet_rescale_ts(pkt, in_stream->time_base, out_stream->time_base);
    pkt->stream_index = out_index;
    pkt->pos = -1;

    ret = av_interleaved_write_frame(ofmt_ctx, pkt);
    av_packet_unref(pkt);
    if (ret < 0) {
      break;
    }
  }

  av_packet_free(&pkt);
  if (ret == AVERROR_EOF) {
    return 0;
  }
  return ret < 0 ? ret : 0;
}

static int transcode_mode_run(AVFormatContext *ifmt_ctx, AVFormatContext *ofmt_ctx,
                              std::vector<StreamContext> &streams,
                              const std::unordered_map<int, int> &in_to_ctx,
                              int64_t input_duration_us) {
  AVPacket *pkt = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();
  if (!pkt || !frame) {
    av_packet_free(&pkt);
    av_frame_free(&frame);
    return AVERROR(ENOMEM);
  }

  int64_t last_progress_us = -500000;
  int ret = 0;
  // 主循环：读输入 packet -> 送解码器 -> 收 frame -> 送编码器 -> 写输出 packet。
  while ((ret = av_read_frame(ifmt_ctx, pkt)) >= 0) {
    auto it = in_to_ctx.find(pkt->stream_index);
    if (it == in_to_ctx.end()) {
      av_packet_unref(pkt);
      continue;
    }

    maybe_print_progress("Transcode progress:", packet_pts_to_us(pkt, ifmt_ctx->streams[pkt->stream_index]),
                         input_duration_us, last_progress_us);

    StreamContext &sc = streams[it->second];
    ret = avcodec_send_packet(sc.dec_ctx, pkt);
    av_packet_unref(pkt);
    if (ret < 0) {
      break;
    }

    while ((ret = avcodec_receive_frame(sc.dec_ctx, frame)) >= 0) {
      if (sc.type == AVMEDIA_TYPE_VIDEO) {
        ret = encode_video_frame(sc, frame, ifmt_ctx, ofmt_ctx);
      } else if (sc.type == AVMEDIA_TYPE_AUDIO) {
        ret = fifo_write_audio(sc, frame);
        if (ret >= 0) {
          ret = encode_audio_from_fifo(sc, ofmt_ctx, false);
        }
      }
      av_frame_unref(frame);
      if (ret < 0) {
        break;
      }
    }

    if (ret == AVERROR(EAGAIN)) {
      ret = 0;
    } else if (ret == AVERROR_EOF) {
      ret = 0;
    } else if (ret < 0) {
      break;
    }
  }

  if (ret == AVERROR_EOF) {
    ret = 0;
  }

  // Flush decoders:
  // 告诉解码器“输入结束”，把内部缓存中的帧全部取完。
  if (ret >= 0) {
    for (auto &sc : streams) {
      ret = avcodec_send_packet(sc.dec_ctx, nullptr);
      if (ret < 0) {
        break;
      }

      while ((ret = avcodec_receive_frame(sc.dec_ctx, frame)) >= 0) {
        if (sc.type == AVMEDIA_TYPE_VIDEO) {
          ret = encode_video_frame(sc, frame, ifmt_ctx, ofmt_ctx);
        } else if (sc.type == AVMEDIA_TYPE_AUDIO) {
          ret = fifo_write_audio(sc, frame);
          if (ret >= 0) {
            ret = encode_audio_from_fifo(sc, ofmt_ctx, false);
          }
        }
        av_frame_unref(frame);
        if (ret < 0) {
          break;
        }
      }
      if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
        ret = 0;
      }
      if (ret < 0) {
        break;
      }
    }
  }

  // Flush encoders:
  // 告诉编码器“没有新帧”，把 B 帧重排等残留 packet 全部输出。
  if (ret >= 0) {
    for (auto &sc : streams) {
      if (sc.type == AVMEDIA_TYPE_VIDEO) {
        ret = avcodec_send_frame(sc.enc_ctx, nullptr);
        if (ret < 0) {
          break;
        }
        ret = write_encoded_packets(sc, ofmt_ctx);
      } else if (sc.type == AVMEDIA_TYPE_AUDIO) {
        ret = encode_audio_from_fifo(sc, ofmt_ctx, true);
      }
      if (ret < 0) {
        break;
      }
    }
  }

  av_packet_free(&pkt);
  av_frame_free(&frame);
  return ret;
}

static void cleanup_streams(std::vector<StreamContext> &streams) {
  // 集中释放，保证任意失败路径都不会泄漏资源。
  for (auto &sc : streams) {
    if (sc.audio_fifo) {
      av_audio_fifo_free(sc.audio_fifo);
      sc.audio_fifo = nullptr;
    }
    if (sc.swr_ctx) {
      swr_free(&sc.swr_ctx);
    }
    if (sc.sws_ctx) {
      sws_freeContext(sc.sws_ctx);
      sc.sws_ctx = nullptr;
    }
    if (sc.dec_ctx) {
      avcodec_free_context(&sc.dec_ctx);
    }
    if (sc.enc_ctx) {
      avcodec_free_context(&sc.enc_ctx);
    }
  }
}

int main(int argc, char **argv) {
  // 入口流程分为 8 步：
  // 1) 解析参数
  // 2) 打开输入并探测流
  // 3) 选择音视频流
  // 4) 初始化输出、解码器、编码器
  // 5) 写 MP4 头
  // 6) 执行 copy 或 transcode 主循环
  // 7) 写尾并关闭文件
  // 8) 统一清理并按错误码退出
  Options opt;
  if (!parse_args(argc, argv, opt)) {
    return EXIT_ARG_ERROR;
  }

  AVFormatContext *ifmt_ctx = nullptr;
  AVFormatContext *ofmt_ctx = nullptr;
  AVDictionary *mux_opts = nullptr;
  std::vector<StreamContext> streams;
  std::unordered_map<int, int> in_to_ctx;
  std::unordered_map<int, int> in_to_out;
  int64_t input_duration_us = AV_NOPTS_VALUE;
  int exit_code = EXIT_OK;

  int ret = avformat_open_input(&ifmt_ctx, opt.input_path.c_str(), nullptr, nullptr);
  if (ret < 0) {
    log_msg(LogLevel::kError, "avformat_open_input failed: " + ff_err2str(ret));
    return EXIT_INPUT_OPEN_ERROR;
  }

  ret = avformat_find_stream_info(ifmt_ctx, nullptr);
  if (ret < 0) {
    log_msg(LogLevel::kError, "avformat_find_stream_info failed: " + ff_err2str(ret));
    avformat_close_input(&ifmt_ctx);
    return EXIT_INPUT_PROBE_ERROR;
  }
  input_duration_us = ifmt_ctx->duration;

  int video_index = opt.video_index >= 0 ? opt.video_index : select_first_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO);
  int audio_index = select_audio_stream(ifmt_ctx, opt);
  if (video_index < 0 && audio_index < 0) {
    log_msg(LogLevel::kError, "No usable video/audio stream found.");
    avformat_close_input(&ifmt_ctx);
    return EXIT_STREAM_SELECT_ERROR;
  }

  ret = avformat_alloc_output_context2(&ofmt_ctx, nullptr, nullptr, opt.output_path.c_str());
  if (ret < 0 || !ofmt_ctx) {
    log_msg(LogLevel::kError, "avformat_alloc_output_context2 failed.");
    avformat_close_input(&ifmt_ctx);
    return EXIT_OUTPUT_CREATE_ERROR;
  }

  auto add_stream = [&](int input_index, AVMediaType type) -> int {
    if (input_index < 0 || input_index >= static_cast<int>(ifmt_ctx->nb_streams)) {
      return AVERROR(EINVAL);
    }
    if (ifmt_ctx->streams[input_index]->codecpar->codec_type != type) {
      return AVERROR(EINVAL);
    }
    StreamContext sc;
    sc.type = type;
    sc.input_index = input_index;
    streams.push_back(sc);
    in_to_ctx[input_index] = static_cast<int>(streams.size() - 1);
    return 0;
  };

  if (video_index >= 0) {
    ret = add_stream(video_index, AVMEDIA_TYPE_VIDEO);
    if (ret < 0) {
      log_msg(LogLevel::kError, "Invalid --video-index.");
      exit_code = EXIT_STREAM_SELECT_ERROR;
      goto done;
    }
  }
  if (audio_index >= 0) {
    ret = add_stream(audio_index, AVMEDIA_TYPE_AUDIO);
    if (ret < 0) {
      log_msg(LogLevel::kError, "Invalid --audio-index or --audio-lang result.");
      exit_code = EXIT_STREAM_SELECT_ERROR;
      goto done;
    }
  }

  for (auto &sc : streams) {
    if (opt.copy_mode) {
      // copy 模式（remux）核心思想：
      // - 不解码、不编码，只把输入的压缩包重新封装到新容器
      // - 因为不涉及像素/采样数据处理，所以不需要 dec_ctx / enc_ctx
      // - 但输出容器仍然需要“流描述信息”，即 codecpar
      AVStream *in_stream = ifmt_ctx->streams[sc.input_index];
      // 在输出容器中创建一条新流（和输入流一一对应）。
      // 注意：这里传 nullptr，表示由我们手动拷贝 codecpar，而不是绑定某个 encoder。
      AVStream *out_stream = avformat_new_stream(ofmt_ctx, nullptr);
      if (!out_stream) {
        // 创建输出流失败通常是内存不足或上下文异常。
        ret = AVERROR(ENOMEM);
        exit_code = EXIT_OUTPUT_CREATE_ERROR;
        goto done;
      }
      // 记录“当前 StreamContext 对应到输出容器里的哪一路流”，
      // 后续写 packet 时会用到这个 output_index。
      sc.output_index = out_stream->index;
      // 建立输入流索引 -> 输出流索引映射：
      // 主循环读到输入 packet 后，可快速找到该写到哪一路输出流。
      in_to_out[sc.input_index] = out_stream->index;

      // 把输入流的编码参数完整拷贝到输出流：
      // codec_id / profile / extradata / 声道信息 / 分辨率等都在 codecpar 里。
      // 这是 remux 成功的关键步骤之一。
      ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
      if (ret < 0) {
        // 参数拷贝失败，无法保证输出流描述正确，必须终止。
        exit_code = EXIT_OUTPUT_CREATE_ERROR;
        goto done;
      }

      // 置 0 让 muxer 重新选择合适的 codec_tag，避免“旧容器遗留 tag”
      // 导致目标容器（这里是 MP4）不兼容或写头失败。
      out_stream->codecpar->codec_tag = 0;

      // copy 模式下沿用输入流时间基，保证后续 packet 做时间戳重映射时
      // 有一致的“输入刻度”可参考，减少同步问题风险。
      out_stream->time_base = in_stream->time_base;
      // 当前流已完成 copy 模式初始化，进入下一路流。
      continue;
    }

    ret = open_decoder(ifmt_ctx, sc);
    if (ret < 0) {
      exit_code = EXIT_DECODE_ERROR;
      goto done;
    }

    if (sc.type == AVMEDIA_TYPE_VIDEO) {
      ret = open_video_encoder(opt, ifmt_ctx, ofmt_ctx, sc);
    } else {
      ret = open_audio_encoder(opt, ofmt_ctx, sc);
    }
    if (ret < 0) {
      exit_code = EXIT_ENCODE_ERROR;
      goto done;
    }
    in_to_out[sc.input_index] = sc.output_index;
  }

  if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    ret = avio_open(&ofmt_ctx->pb, opt.output_path.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0) {
      log_msg(LogLevel::kError, "avio_open failed: " + ff_err2str(ret));
      exit_code = EXIT_MUX_ERROR;
      goto done;
    }
  }

  av_dict_set(&mux_opts, "movflags", "+faststart", 0);
  // +faststart 会把 moov 提前，提升网络场景首开速度。
  ret = avformat_write_header(ofmt_ctx, &mux_opts);
  av_dict_free(&mux_opts);
  if (ret < 0) {
    log_msg(LogLevel::kError, "avformat_write_header failed: " + ff_err2str(ret));
    exit_code = EXIT_MUX_ERROR;
    goto done;
  }

  log_msg(LogLevel::kInfo, "Start processing...");
  if (opt.copy_mode) {
    ret = copy_mode_run(ifmt_ctx, ofmt_ctx, in_to_out, input_duration_us);
  } else {
    ret = transcode_mode_run(ifmt_ctx, ofmt_ctx, streams, in_to_ctx, input_duration_us);
  }
  if (ret < 0) {
    log_msg(LogLevel::kError, "Processing failed: " + ff_err2str(ret));
    exit_code = opt.copy_mode ? EXIT_MUX_ERROR : EXIT_RUNTIME_ERROR;
    goto done;
  }

  ret = av_write_trailer(ofmt_ctx);
  if (ret < 0) {
    log_msg(LogLevel::kError, "av_write_trailer failed: " + ff_err2str(ret));
    exit_code = EXIT_MUX_ERROR;
    goto done;
  }
  std::cout << "\r";
  log_msg(LogLevel::kInfo, "Done. Output: " + opt.output_path);

done:
  cleanup_streams(streams);
  if (ifmt_ctx) {
    avformat_close_input(&ifmt_ctx);
  }
  if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE) && ofmt_ctx->pb) {
    avio_closep(&ofmt_ctx->pb);
  }
  if (ofmt_ctx) {
    avformat_free_context(ofmt_ctx);
  }

  if (ret < 0) {
    if (exit_code == EXIT_OK) {
      return EXIT_RUNTIME_ERROR;
    }
    return exit_code;
  }
  return EXIT_OK;
}
