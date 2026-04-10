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

// 每一路参与处理的流（最多视频一路 + 音频一路）都对应一个 StreamContext。
// 你可以把它理解成“程序内部对某一路流的总控对象”：
// - 上游它知道自己对应输入文件里的哪一路流（input_index）
// - 中间它持有这一路流需要的解码器/编码器/转换器
// - 下游它知道自己最终要写到输出文件里的哪一路流（output_index）
//
// 这样设计的好处是：主循环读到一个输入 packet 后，只要先找到它对应的 StreamContext，
// 后续就能顺着这个对象完成“解码 -> 格式转换 -> 编码 -> 写输出”的整条链路，
// 避免把 video_dec_ctx / audio_dec_ctx / video_enc_ctx / audio_enc_ctx 等状态分散在一堆全局变量里。
struct StreamContext {
  AVMediaType type = AVMEDIA_TYPE_UNKNOWN; // 流类型：视频流或音频流（决定后续走哪条处理逻辑）
  int input_index = -1;                    // 输入容器中的流索引（来自 ifmt_ctx->streams[]）
  int output_index = -1;                   // 输出容器中的流索引（对应 ofmt_ctx->streams[]）

  AVCodecContext *dec_ctx = nullptr;       // 解码器上下文：把压缩 packet 解成原始 frame
  AVCodecContext *enc_ctx = nullptr;       // 编码器上下文：把原始 frame 编回目标编码 packet

  // SwsContext 名字来源：SoftWare Scale (软件缩放/图像处理库)。
  // 它是 FFmpeg 中 libswscale 库的核心结构体，专门用于【视频/图像】的缩放、像素格式转换（如 RGB 转 YUV）和色彩空间转换。
  SwsContext *sws_ctx = nullptr;           // 视频像素格式/尺寸转换上下文（例如转为 yuv420p）
  
  // SwrContext 名字来源：SoftWare Resample (软件重采样/音频处理库)。
  // 它是 FFmpeg 中 libswresample 库的核心结构体，专门用于【音频】的重采样（如 48kHz 转 44.1kHz）、采样格式转换（如 f32 转 s16）和声道布局转换（如 5.1 转立体声）。
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

  // av_frame_get_buffer 作用：为音视频帧分配实际的数据内存（Data Buffer）。
  // 详细机制：
  // 1. 分配前提：在调用它之前，必须先告诉它你要多大的房子。所以上面必须先设置好帧的“元数据”（视频的 format、width、height）。
  // 2. 核心动作：av_frame_alloc() 只是创建了一个“空壳子”（AVFrame 结构体），并没有分配存放几百万个像素点的巨大内存。
  //    av_frame_get_buffer 会根据宽高和像素格式（如 YUV420P），计算出需要的总字节数，真正去申请这块内存，
  //    并把内存地址正确地挂载到 frame->data 数组中，同时计算好每行的跨度（frame->linesize）。
  // 3. 参数 0 的含义（内存对齐）：第二个参数代表内存对齐（Alignment）。传 0 表示让 FFmpeg 自动使用默认的最优对齐方式（通常是 32 或 64 字节对齐）。
  //    【为什么需要对齐？】因为底层的音视频处理（如 Swscale 缩放、H.264 编码）大量使用了 CPU 的 SIMD 硬件加速指令集（如 SSE/AVX）。
  //    这些高级指令要求数据必须严格存放在特定的内存边界上，否则程序会直接崩溃或极度掉速。
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

  // sws_scale 作用：执行真正的视频帧图像转换（包括分辨率缩放 + 像素格式转换）。
  // 详细机制与参数解析：
  // 1. sc.sws_ctx：转换上下文，里面已经配置好了源和目标的宽高、像素格式以及使用的缩放算法（如 SWS_BILINEAR）。
  // 2. decoded->data / linesize：【输入源】解码出来的原始图像数据指针数组，以及每个平面的行跨度（步长）。
  // 3. 0：从输入图像的第 0 行（最顶端）开始处理（srcSliceY）。
  // 4. sc.dec_ctx->height：总共要处理多少行（srcSliceH）。
  //    【为什么只传 height 不传 width？】
  //    因为 width 信息已经在之前创建 sc.sws_ctx 时（sws_getCachedContext）固定写死了。
  //    sws_scale 设计为支持“分片（Slice）处理”以节省内存或多线程加速。
  //    分片只能是横向切分（按行切），不能纵向切分，所以每次调用只需要告诉它“这次处理从第几行开始，一共处理多少行”即可。
  //    这里传 0 和 height，表示不分片，一次性处理整张图。
  // 5. frame->data / linesize：【输出目标】转换后的数据要写到哪里去，即我们之前用 av_frame_get_buffer 分配好内存的目标帧。
  // 
  // 【为什么需要这步？】
  // 解码器吐出的原始帧（decoded）可能是 1080p、NV12 格式；而我们的编码器（enc_ctx）可能被配置为要求 720p、YUV420P 格式。
  // sws_scale 就负责把原始画面“重绘”成编码器严格要求的样子，然后填入目标 frame 中。
  sws_scale(sc.sws_ctx,
            decoded->data,
            decoded->linesize,
            0,
            sc.dec_ctx->height,
            frame->data,
            frame->linesize);

  // 优先用 FFmpeg 为解码帧推断出的“最可靠时间戳”。
  int64_t src_pts = decoded->best_effort_timestamp;
  if (src_pts == AV_NOPTS_VALUE) {
    // 如果没有可用的 best_effort_timestamp，再退回原始 pts。
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
  // 1. 计算重采样器中缓存的延迟样本数（基于解码器的采样率）
  // 为什么必须获取 delay？
  // FFmpeg 的重采样器（SwrContext）内部带有缓存（多相 FIR 滤波器等），
  // 输入和输出并非严格同步。上次转换可能遗留了部分样本在内部缓存中。
  // 如果不计算 delay，仅根据当前帧的样本数（decoded->nb_samples）分配内存，
  // 当重采样器将历史遗留样本 + 新样本一起输出时，会导致实际输出样本数大于分配空间，
  // 从而引发内存越界写（Crash）或数据截断。
  // 因此，必须询问重采样器内部残留了多少样本，并与当前帧样本数相加，才能算出绝对安全的缓冲区大小。
  const int64_t delay = swr_get_delay(sc.swr_ctx, sc.dec_ctx->sample_rate);
  
  // 2. 计算目标采样数：将 (延迟样本数 + 当前帧样本数) 从解码器采样率转换到编码器采样率，并向上取整
  const int dst_nb_samples = av_rescale_rnd(
      delay + decoded->nb_samples,
      sc.enc_ctx->sample_rate,
      sc.dec_ctx->sample_rate,
      AV_ROUND_UP);

  // 3. 为重采样后的音频数据分配内存空间
  // TODO: 性能优化点：目前每次调用都会重新分配和释放内存（converted_data），
  // 这在音频处理这种高频调用的场景下效率较低。
  // 更好的做法是将 converted_data 和其容量（allocated_samples）作为 StreamContext 的成员变量，
  // 仅在 dst_nb_samples 大于当前容量时才进行 realloc，从而复用内存。
  uint8_t **converted_data = nullptr;
  // 根据编码器的声道数、计算出的目标采样数和编码器的采样格式来分配数据指针数组和实际的数据缓冲区
  int ret = av_samples_alloc_array_and_samples(
      &converted_data,
      nullptr,
      sc.enc_ctx->ch_layout.nb_channels,
      dst_nb_samples,
      sc.enc_ctx->sample_fmt,
      0);
  if (ret < 0) {
    return ret; // 内存分配失败，返回错误码
  }

  // 将解码出的音频转换成编码器目标格式，再写入 FIFO。
  // 4. 执行重采样/格式转换操作
  // 将 decoded 中的音频数据转换为目标格式并存入 converted_data 中，返回实际转换的样本数
  ret = swr_convert(
      sc.swr_ctx,
      converted_data,
      dst_nb_samples,
      const_cast<const uint8_t **>(decoded->extended_data),
      decoded->nb_samples);
  if (ret < 0) {
    // 转换失败，释放之前分配的内存（先释放数据区，再释放指针数组）
    av_freep(&converted_data[0]);
    av_freep(&converted_data);
    return ret;
  }

  const int converted_samples = ret; // 实际转换得到的样本数
  
  // 5. 重新分配音频 FIFO 的大小，确保有足够的空间容纳 FIFO 中原有的数据加上新转换的数据
  // 注意：av_audio_fifo_realloc 内部有容量判断机制。
  // 只有当请求的样本总数（当前大小 + 新转换大小）大于 FIFO 底层实际已分配的物理容量时，
  // 才会真正执行内存重新分配（realloc）。如果底层容量足够，它会直接返回成功，不会产生性能损耗。
  if (av_audio_fifo_realloc(sc.audio_fifo, av_audio_fifo_size(sc.audio_fifo) + converted_samples) < 0) {
    av_freep(&converted_data[0]);
    av_freep(&converted_data);
    return AVERROR(ENOMEM); // 内存不足
  }

  // 6. 将转换后的音频数据写入到 FIFO 缓冲区中
  // 返回值 wrote 表示实际成功写入 FIFO 的样本数（每个声道的样本数）。
  // 正常情况下，它应该等于请求写入的 converted_samples。
  const int wrote = av_audio_fifo_write(sc.audio_fifo, reinterpret_cast<void **>(converted_data), converted_samples);
  
  // 7. 写入完成后，释放临时分配的转换缓冲区
  av_freep(&converted_data[0]);
  av_freep(&converted_data);
  
  // 8. 检查实际写入 FIFO 的样本数是否等于预期转换的样本数
  if (wrote < converted_samples) {
    return AVERROR(EIO); // 写入失败或未完全写入，返回 I/O 错误
  }
  return 0; // 成功处理
}

static int encode_audio_from_fifo(StreamContext &sc, AVFormatContext *ofmt_ctx, bool flush) {
  // 1. 检查编码器是否支持可变帧长（Variable Frame Size）
  // 大多数音频编码器（如 AAC）要求每次输入固定数量的样本（通常是 1024）。
  // 少数编码器（如 Vorbis、Opus）允许每次输入任意数量的样本。
  const bool variable_frame_size =
      (sc.enc_ctx->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) != 0;
  
  // 获取编码器要求的固定帧长，如果未指定则默认给 1024
  const int frame_size = sc.enc_ctx->frame_size > 0 ? sc.enc_ctx->frame_size : 1024;

  // 2. 循环判断 FIFO 中的数据是否足够凑成一帧
  // 条件 A：FIFO 中的样本数 >= 编码器要求的帧长（正常持续编码情况）
  // 条件 B：当前是 flush 阶段（文件末尾），且 FIFO 中还有残留数据（即使不够一帧也要强行提取处理）
  while (av_audio_fifo_size(sc.audio_fifo) >= frame_size ||
         (flush && av_audio_fifo_size(sc.audio_fifo) > 0)) {
    
    // 3. 计算本次需要读取的样本数和需要分配的帧大小
    const int fifo_size = av_audio_fifo_size(sc.audio_fifo);
    // 如果支持可变帧长，就把 FIFO 里的数据全读出来；否则严格按照固定帧长读取
    const int read_samples = variable_frame_size ? fifo_size : frame_size;
    // 分配的样本数通常等于读取的样本数，但如果是固定帧长，无论实际能读出多少（flush时可能不足），都要分配固定大小的空间
    const int alloc_samples = variable_frame_size ? read_samples : frame_size;

    // 4. 分配并初始化 AVFrame
    AVFrame *frame = av_frame_alloc();
    if (!frame) {
      return AVERROR(ENOMEM);
    }
    frame->nb_samples = alloc_samples;
    frame->format = sc.enc_ctx->sample_fmt;
    av_channel_layout_copy(&frame->ch_layout, &sc.enc_ctx->ch_layout);
    frame->sample_rate = sc.enc_ctx->sample_rate;

    // 根据上面设置的参数（采样率、格式、声道数、样本数），为 frame 分配实际的数据缓冲区
    int ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
      av_frame_free(&frame);
      return ret;
    }

    // 5. 从 FIFO 中读取数据到 frame 的缓冲区中
    const int actual_read = av_audio_fifo_read(sc.audio_fifo, reinterpret_cast<void **>(frame->data), read_samples);
    if (actual_read < 0) {
      av_frame_free(&frame);
      return AVERROR(EIO);
    }
    
    // 6. 尾部静音填充（Padding / Silence）
    // 对于固定帧长编码器（如常见 AAC），每次必须严格输入 frame_size 个样本。
    // 在 flush 尾声时，FIFO 里剩下的数据可能不足 frame_size，此时需要将缺失的部分补上静音数据，否则编码器会报错。
    if (!variable_frame_size && actual_read < frame_size) {
      av_samples_set_silence(
          frame->data,
          actual_read,                 // 从哪个偏移量开始补静音
          frame_size - actual_read,    // 需要补多少个样本的静音
          sc.enc_ctx->ch_layout.nb_channels,
          sc.enc_ctx->sample_fmt);
    }

    // 7. 设置时间戳（PTS）
    // 音频时间轴通常以“采样点”为单位推进（即 time_base = 1 / sample_rate）。
    // 所以下一帧的 PTS 就是当前 PTS 加上本帧包含的样本数。
    frame->pts = sc.next_audio_pts;
    sc.next_audio_pts += frame->nb_samples;

    // 8. 发送给编码器进行编码
    ret = avcodec_send_frame(sc.enc_ctx, frame);
    av_frame_free(&frame); // 发送后即可释放 frame（编码器内部会接管或拷贝数据）
    if (ret < 0) {
      return ret;
    }
    
    // 9. 尝试从编码器中读取编码后的数据包（Packet）并写入输出文件
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
    // 1. 转换时间戳（PTS/DTS）的时间基（Time Base），保证音视频在新的文件中播放速度和音画同步是正常的
    // 注：out_stream->time_base 是在调用 avformat_write_header() 时，由 FFmpeg 的封装器（Muxer）自动确定并分配的。
    // 分配规则因输出容器格式（如 MP4, FLV, TS）而异：
    // - FLV：固定分配为 1/1000（毫秒精度）。
    // - MPEG-TS：固定分配为 1/90000。
    // - MP4/MOV：通常会根据视频帧率或音频采样率计算出一个合适的高精度时间基（例如 1/12800 或 1/90000）。
    av_packet_rescale_ts(pkt, in_stream->time_base, out_stream->time_base);
    // 2. 更新数据包所属的流索引，告诉封装器该写到哪个轨道
    pkt->stream_index = out_index;
    // 3. 重置数据包在输入文件中的物理字节偏移量，避免误导输出封装器
    pkt->pos = -1;

    // 4. 将数据包按时间顺序交织（音频和视频交替）写入输出文件
    // av_interleaved_write_frame 内部机制非常复杂，它不仅仅是一个“Write”函数，更是一个“路由器+排序器+调度器”：
    // - 内部缓存 (Buffering)：通常不会立刻把数据写到磁盘上，而是先把它存放到 FFmpeg 内部的一个缓存队列中。
    // - 按时间戳排序 (Sorting by DTS)：检查缓存队列中所有数据包的解码时间戳（DTS）。因为输入包的时间线可能不是严格递增的，它会负责把这些包重新按时间先后顺序排好。
    // - 交织打包 (Interleaving) - 核心动作：把不同轨道（视频流、音频流）的数据包，按照时间顺序“像拉链一样”交错排列在一起。
    //   【为什么必须交织？】如果不交织，文件前半部分全是视频，后半部分全是音频。播放器为了同时播放音视频，必须跳到文件末尾去读音频，导致：
    //   1. 本地播放：磁盘磁头疯狂来回寻址（Seek），极其卡顿。
    //   2. 网络播放：根本无法边下边播，必须把整个文件下载完才能播放。
    //   3. 内存爆炸：如果不想来回寻址，就得把前半部分的所有视频都缓存在内存里等音频。
    //   交织之后，物理文件上相邻的数据块，在播放时间上也是相邻的，顺着读就能流畅播放。
    // - 真正写入底层 IO (Flushing)：当内部缓存收集了足够多的包，能够绝对确定谁的时间戳最靠前时，就会把最老的那个包剥离出来，真正调用底层的 IO 接口（如 avio_write）写入输出文件。
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

    // packet_pts_to_us 作用：将数据包的显示时间戳（PTS）转换为绝对的微秒数（Microseconds）。
    // 详细机制：
    // 1. PTS 只是一个相对刻度：在 FFmpeg 中，pkt->pts 只是一个整数（比如 90000），它本身不代表具体的秒数。
    //    必须结合该流的时间基 stream->time_base（比如 1/90000 秒）才能算出真实时间（90000 * 1/90000 = 1秒）。
    // 2. 统一时间单位：视频流和音频流的时间基往往不同。为了统一计算转码进度，必须把它们都转换成一个统一的标准时间单位。
    // 3. 内部实现：它调用了 av_rescale_q(pts, stream->time_base, AV_TIME_BASE_Q)。
    //    - AV_TIME_BASE_Q 是 FFmpeg 定义的标准微秒时间基（1/1000000）。
    //    - av_rescale_q 是一个安全的数学函数，用于在不同时间基之间按比例缩放时间戳，能有效防止直接乘除导致的整数溢出。
    // 最终，这里获取到当前包的绝对微秒时间，传给 maybe_print_progress，用于和总时长对比，打印出当前的转码进度（如 50%）。

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
  // 容器上下文
  AVFormatContext *ifmt_ctx = nullptr;
  AVFormatContext *ofmt_ctx = nullptr;
  
  // 存放复用器(Muxer)配置参数的字典，例如 mp4 的 movflags=faststart。
  // 初始化为 nullptr，后续调用 av_dict_set 时 FFmpeg 会自动分配内存。
  AVDictionary *mux_opts = nullptr;
  
  std::vector<StreamContext> streams;

  // 输入流索引 -> StreamContext 下标
  // 用途：转码模式下，主循环 av_read_frame 读到一个输入 packet 后，
  // 先通过 pkt.stream_index 找到它属于 streams 里的哪一个 StreamContext，
  // 然后才能拿到对应的 dec_ctx / enc_ctx / sws_ctx / swr_ctx 等状态继续处理。
  // 例子：
  //   输入文件的视频流索引可能是 0，音频流索引可能是 2；
  //   但程序内部只挑了两路流参与处理，于是可能建立：
  //     in_to_ctx[0] = 0   // 输入视频流 0 -> streams[0]
  //     in_to_ctx[2] = 1   // 输入音频流 2 -> streams[1]
  std::unordered_map<int, int> in_to_ctx;
  // 输入流索引 -> 输出流索引
  // 用途：copy/remux 模式下不经过解码器和编码器，读到输入 packet 后，
  // 需要立刻知道它最终该写到输出文件的哪一路流里。
  // 例子：
  //   输入视频流是 0、音频流是 2；
  //   输出文件中它们可能变成输出流 0 和 1，于是：
  //     in_to_out[0] = 0
  //     in_to_out[2] = 1
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

  // 为输出文件创建封装上下文（AVFormatContext）。
  // FFmpeg 会根据输出路径的后缀（例如 .mp4 / .flv / .ts）自动推断要使用哪种 muxer，
  // 并把创建好的输出上下文写到 ofmt_ctx，后续新建输出流、写文件头、写包都依赖它。
  ret = avformat_alloc_output_context2(&ofmt_ctx, nullptr, nullptr, opt.output_path.c_str());
  if (ret < 0 || !ofmt_ctx) {
    // 如果输出上下文创建失败，说明输出容器还没法正常建立，
    // 后续的输出流创建和写 header 都无法继续，因此记录错误、关闭已打开的输入文件并返回。
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

  // 将 MP4/MOV 封装选项 "movflags" 设置为 "+faststart"
  // 默认情况下，MP4 文件的 moov atom（包含视频元数据如索引、时长等）写在文件末尾。
  // 加上 +faststart 后，FFmpeg 会在封装结束时进行二次处理，把 moov 移动到文件头部。
  // 这对于网络流媒体播放至关重要，因为播放器需要先读取 moov 才能开始播放，放在头部可以实现边下边播（提升首开速度）。
  av_dict_set(&mux_opts, "movflags", "+faststart", 0);
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
