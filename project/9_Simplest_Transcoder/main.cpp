// 最简转码器：读 MP4 → decode → 缩放降分辨率 → 用 libx264 重新 encode → 写新 MP4
// 对应 Doc/ffmpeg/99-学习进度.md 优先级 2。是播放器的"镜像"——播放器解码后送显示,
// 转码器解码后再编码送文件。编码侧用 send_frame/receive_packet,和解码 send/receive 对称。
// 📖 逐步原理讲解(配套复习文档)：见同目录 逐步讲解.md
//
// 整体框架(4 步,逐步填充):
//   T1 打开输入、找视频流、建解码器
//   T2 建缩放器(SwsContext 降分辨率) + 建编码器(libx264)
//   T3 建输出容器 + 加输出流 + 写文件头
//   T4 转码循环(读→解→缩放→编码→rescale 时间戳→写) + flush + 写尾   ← 本步已完成(全流程通)
//
// 用法: ./simplest_transcoder <输入.mp4> <输出.mp4>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>       // av_opt_set：给 libx264 设 crf / preset 这类私有参数
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

    // ===== T2-a：缩放器——把解码出的帧从原尺寸缩到 640x360(仍是 YUV420P)=====
    // 和播放器⑤同样的 SwsContext,只是这次目标不是 RGB 而是"小一号的 YUV"(编码器吃 YUV)。
    SwsContext *scalerContext = sws_getContext(
        decoderContext->width, decoderContext->height, decoderContext->pix_fmt,  // 源
        kOutputWidth, kOutputHeight, AV_PIX_FMT_YUV420P,                          // 目标
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    // ===== T2-b：建编码器(libx264)——解码的镜像,把 YUV 帧压成 H.264 包 =====
    const AVCodec *encoder = avcodec_find_encoder_by_name("libx264");
    if (!encoder) {
        std::fprintf(stderr, "找不到 libx264 编码器\n");
        return 1;
    }
    AVCodecContext *encoderContext = avcodec_alloc_context3(encoder);
    encoderContext->width = kOutputWidth;
    encoderContext->height = kOutputHeight;
    encoderContext->pix_fmt = AV_PIX_FMT_YUV420P;   // H.264 最通用的像素格式
    // time_base:编码器的"时间单位"。沿用输入流的 time_base,这样解码帧的 pts 能直接喂给编码器。
    encoderContext->time_base = inputVideoStream->time_base;
    encoderContext->framerate = inputVideoStream->avg_frame_rate;
    encoderContext->gop_size = 12;                  // 关键帧间隔(每 12 帧一个 I 帧,见 05 §GOP)
    // CRF / preset 是 libx264 的私有参数,通过 priv_data 设(见 06)。
    av_opt_set(encoderContext->priv_data, "crf", kCrf, 0);
    av_opt_set(encoderContext->priv_data, "preset", kPreset, 0);
    // 我们固定输出 mp4,mp4 要求 SPS/PPS 放在容器头(global header)而不是每个包里。
    // 所以这里直接设这个 flag;通用写法是建完输出容器后判 oformat->flags & AVFMT_GLOBALHEADER。
    encoderContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(encoderContext, encoder, nullptr) < 0) {
        std::fprintf(stderr, "打开编码器失败\n");
        return 1;
    }
    std::printf("✅ 缩放器 + 编码器就绪\n");

    // ===== T3：建输出容器 + 加输出流 + 写文件头 =====
    // 输入侧用 avformat_open_input(读),输出侧用 avformat_alloc_output_context2(写)。
    // 第三/四参传 nullptr,让它从输出路径的后缀(.mp4)推断封装格式。
    AVFormatContext *outputFormatContext = nullptr;
    avformat_alloc_output_context2(&outputFormatContext, nullptr, nullptr, outputPath);
    if (!outputFormatContext) {
        std::fprintf(stderr, "建不了输出容器(后缀认不出?)\n");
        return 1;
    }

    // 给输出容器加一路视频流,并把"编码器的参数"拷进这路流的 codecpar(让容器知道流长啥样)。
    // 注意方向:T1 是 parameters_to_context(流→解码器),这里是 from_context(编码器→流),正好相反。
    AVStream *outputStream = avformat_new_stream(outputFormatContext, nullptr);
    avcodec_parameters_from_context(outputStream->codecpar, encoderContext);
    outputStream->time_base = encoderContext->time_base;

    // 打开输出文件(有些格式不落地文件才跳过,mp4 要)。
    if (!(outputFormatContext->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&outputFormatContext->pb, outputPath, AVIO_FLAG_WRITE) < 0) {
            std::fprintf(stderr, "打不开输出文件: %s\n", outputPath);
            return 1;
        }
    }
    // 写文件头(容器的元信息)。这一步之后,outputStream->time_base 可能被 muxer 改成它偏好的值,
    // 所以 T4 写包时要按"改过之后"的 time_base 来 rescale。
    if (avformat_write_header(outputFormatContext, nullptr) < 0) {
        std::fprintf(stderr, "写文件头失败\n");
        return 1;
    }
    std::printf("✅ 输出容器就绪,已写文件头: %s\n", outputPath);

    // ===== T4：转码循环 =====
    AVPacket *inputPacket = av_packet_alloc();    // 从输入读出的压缩包
    AVPacket *encodedPacket = av_packet_alloc();   // 编码器吐出的压缩包
    AVFrame *decodedFrame = av_frame_alloc();      // 解码出的原始帧(原尺寸)
    // 缩放后的帧:要先分配自己的像素缓冲(解码帧的缓冲是解码器的,尺寸也不对)。
    AVFrame *scaledFrame = av_frame_alloc();
    scaledFrame->format = AV_PIX_FMT_YUV420P;
    scaledFrame->width = kOutputWidth;
    scaledFrame->height = kOutputHeight;
    av_frame_get_buffer(scaledFrame, 0);

    long writtenPacketCount = 0;

    // 编码并写出:把一帧(或 NULL=flush)送进编码器,把吐出的包逐个 rescale 时间戳后写进输出。
    // 这是解码 send/receive 的镜像——编码是 send_frame / receive_packet。
    auto encodeAndWrite = [&](AVFrame *frameToEncode) {
        if (avcodec_send_frame(encoderContext, frameToEncode) < 0) return;
        while (true) {
            int receiveResult = avcodec_receive_packet(encoderContext, encodedPacket);
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) break;
            if (receiveResult < 0) { std::fprintf(stderr, "编码出错\n"); break; }

            // ★ 关键:编码器的时间戳用的是"编码器 time_base",写进容器前要换算成"输出流 time_base"。
            // 这就是转码里最容易错的一环——两套时间刻度不一致,不换算会快放/慢放/时间戳错乱(见 05 PTS/DTS)。
            av_packet_rescale_ts(encodedPacket, encoderContext->time_base, outputStream->time_base);
            encodedPacket->stream_index = outputStream->index;
            av_interleaved_write_frame(outputFormatContext, encodedPacket);  // 按 dts 交织写入
            av_packet_unref(encodedPacket);
            ++writtenPacketCount;
        }
    };

    // 解码并转码:把解码器吐的每一帧缩放→拷 pts→送编码。封装成 lambda,drain 阶段也复用。
    auto decodeScaleEncode = [&]() {
        while (true) {
            int receiveResult = avcodec_receive_frame(decoderContext, decodedFrame);
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) break;
            if (receiveResult < 0) { std::fprintf(stderr, "解码出错\n"); break; }

            av_frame_make_writable(scaledFrame);  // 复用缓冲前确保可写(COW,见 01 §5.7)
            sws_scale(scalerContext,
                      decodedFrame->data, decodedFrame->linesize, 0, decoderContext->height,
                      scaledFrame->data, scaledFrame->linesize);
            // sws_scale 不搬时间戳,得手动把 pts 接力过去(编码器和输入流同 time_base,可直接拷)。
            scaledFrame->pts = decodedFrame->pts;
            encodeAndWrite(scaledFrame);
            av_frame_unref(decodedFrame);
        }
    };

    // 主循环:只处理视频包,喂解码器。
    while (av_read_frame(inputFormatContext, inputPacket) >= 0) {
        if (inputPacket->stream_index == videoStreamIndex) {
            if (avcodec_send_packet(decoderContext, inputPacket) >= 0) {
                decodeScaleEncode();
            }
        }
        av_packet_unref(inputPacket);
    }

    // 两级 flush:先冲解码器(把攒着的帧解出来转码),再冲编码器(把攒着的包吐出来写)。
    avcodec_send_packet(decoderContext, nullptr);
    decodeScaleEncode();
    encodeAndWrite(nullptr);   // NULL 帧 = 通知编码器 flush

    // 写文件尾(mp4 的 moov 等收尾信息),这步不做文件会损坏/不可播。
    av_write_trailer(outputFormatContext);
    std::printf("✅ 转码完成,写出 %ld 个视频包 → %s\n", writtenPacketCount, outputPath);

    // ===== 清理 =====
    av_frame_free(&scaledFrame);
    av_frame_free(&decodedFrame);
    av_packet_free(&encodedPacket);
    av_packet_free(&inputPacket);
    if (outputFormatContext && !(outputFormatContext->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&outputFormatContext->pb);
    }
    avformat_free_context(outputFormatContext);
    avcodec_free_context(&encoderContext);
    sws_freeContext(scalerContext);
    avcodec_free_context(&decoderContext);
    avformat_close_input(&inputFormatContext);
    return 0;
}
