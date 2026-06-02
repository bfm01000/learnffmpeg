// 最简转码器：读 MP4 → decode → 缩放降分辨率 → 用 libx264 重新 encode → 写新 MP4
// 对应 Doc/ffmpeg/99-学习进度.md 优先级 2。是播放器的"镜像"——播放器解码后送显示,
// 转码器解码后再编码送文件。编码侧用 send_frame/receive_packet,和解码 send/receive 对称。
// 📖 逐步原理讲解(配套复习文档)：见同目录 逐步讲解.md
//
// 整体框架(4 步,逐步填充):
//   T1 打开输入、找视频流、建解码器                  ← 本步已完成
//   T2 建缩放器(SwsContext 降分辨率) + 建编码器(libx264)
//   T3 建输出容器 + 加输出流 + 写文件头
//   T4 转码循环(读→解→缩放→编码→rescale 时间戳→写) + flush + 写尾
//
// 用法: ./simplest_transcoder <输入.mp4> <输出.mp4>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <cstdio>

// 转码目标参数(写死,最简版):降到 640x360,用 CRF 质量模式 + veryfast 预设(见 06)。
static const int kOutputWidth = 640;
static const int kOutputHeight = 360;
static const char *kCrf = "23";          // CRF 越小越清晰/码率越高,23 是常用默认(见 06 §CRF)
static const char *kPreset = "veryfast";  // 预设:编码速度 vs 压缩率的折中(见 06 §Preset)

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "用法: %s <输入.mp4> <输出.mp4>\n", argv[0]);
        return 1;
    }
    const char *inputPath = argv[1];
    const char *outputPath = argv[2];

    // ===== T1：打开输入、找视频流、建解码器 =====
    // 和播放器①②③一样:open_input → find_stream_info → find_best_stream(VIDEO) → 建解码器。
    AVFormatContext *inputFormatContext = nullptr;
    if (avformat_open_input(&inputFormatContext, inputPath, nullptr, nullptr) < 0) {
        std::fprintf(stderr, "打不开输入: %s\n", inputPath);
        return 1;
    }
    if (avformat_find_stream_info(inputFormatContext, nullptr) < 0) {
        std::fprintf(stderr, "读不到流信息\n");
        avformat_close_input(&inputFormatContext);
        return 1;
    }

    int videoStreamIndex =
        av_find_best_stream(inputFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex < 0) {
        std::fprintf(stderr, "输入没有视频流\n");
        avformat_close_input(&inputFormatContext);
        return 1;
    }
    AVStream *inputVideoStream = inputFormatContext->streams[videoStreamIndex];

    const AVCodec *decoder = avcodec_find_decoder(inputVideoStream->codecpar->codec_id);
    AVCodecContext *decoderContext = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(decoderContext, inputVideoStream->codecpar);
    if (avcodec_open2(decoderContext, decoder, nullptr) < 0) {
        std::fprintf(stderr, "打开解码器失败\n");
        avcodec_free_context(&decoderContext);
        avformat_close_input(&inputFormatContext);
        return 1;
    }
    std::printf("✅ 输入: %dx%d, 编码=%s → 目标: %dx%d H.264 (CRF %s, %s)\n",
                decoderContext->width, decoderContext->height, decoder->name,
                kOutputWidth, kOutputHeight, kCrf, kPreset);

    // ===== 后续步骤(占位,逐步填) =====
    // T2 缩放器 + 编码器
    // T3 输出容器 + 流 + 头
    // T4 转码循环 + flush + 尾

    // ===== 清理 =====
    avcodec_free_context(&decoderContext);
    avformat_close_input(&inputFormatContext);
    return 0;
}
