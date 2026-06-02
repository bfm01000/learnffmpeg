// 最简视频播放器：demux MP4 → decode H.264 → swscale 转 RGB → SDL 显示
// 对应 Doc/ffmpeg/99-学习进度.md 优先级 1。只播视频、不含音频。
//
// 整体框架(7 步,逐步填充):
//   ① 解封装：打开文件、找视频流
//   ② 读视频流信息(宽高/编码)
//   ③ 建解码器(AVCodec + AVCodecContext)
//   ④ 解码循环(send_packet / receive_frame)
//   ⑤ YUV → RGB(SwsContext)                        ← 本步已完成
//   ⑥ SDL 开窗、把 RGB 画上去
//   ⑦ 按 pts 控制播放节奏 + drain + 清理
//
// 用法: ./simplest_player <视频文件.mp4>

extern "C" {
// FFmpeg 是 C 库,在 C++ 里必须用 extern "C" 包,否则链接时找不到符号(见 01 §8.1)
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>   // av_get_pix_fmt_name：把像素格式枚举转成可读名("yuv420p")
#include <libavutil/imgutils.h>  // av_image_alloc：按宽高/格式分配一块对齐好的图像缓冲
#include <libswscale/swscale.h>  // SwsContext：缩放 + 像素格式转换(YUV→RGB)
}

#include <cstdio>

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "用法: %s <视频文件.mp4>\n", argv[0]);
        return 1;
    }
    const char *inputPath = argv[1];

    // ===== 阶段①：解封装(demux)——打开文件 =====
    // AVFormatContext 是解封装的"总上下文",统领文件里的各路流(见 01 §一)。
    // 先置 nullptr：avformat_open_input 会负责分配它,出错时也会清理。
    AVFormatContext *formatContext = nullptr;
    if (avformat_open_input(&formatContext, inputPath, nullptr, nullptr) < 0) {
        std::fprintf(stderr, "打不开文件: %s\n", inputPath);
        return 1;
    }
    std::printf("✅ 打开成功: %s\n", inputPath);

    // ===== 阶段②：读视频流信息——找出视频流、读宽高和编码 =====
    // 有些容器(尤其网络流)开头读不到完整信息,要先探测一段数据才知道有几路流、什么编码。
    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        std::fprintf(stderr, "读不到流信息\n");
        avformat_close_input(&formatContext);
        return 1;
    }

    // 一个文件可能有多路流(视频/音频/字幕)。av_find_best_stream 直接帮我们挑出"最佳视频流",
    // 返回它在 formatContext->streams[] 里的下标(找不到返回负数)。
    int videoStreamIndex =
        av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex < 0) {
        std::fprintf(stderr, "文件里没有视频流\n");
        avformat_close_input(&formatContext);
        return 1;
    }
    AVStream *videoStream = formatContext->streams[videoStreamIndex];

    // codecpar(AVCodecParameters)是"流的参数描述":宽高、像素格式、编码 id、extradata(SPS/PPS) 等。
    // 注意它只是"描述",真正解码要用它去建解码器(下一步)。
    AVCodecParameters *codecParameters = videoStream->codecpar;
    std::printf("视频流 #%d, %dx%d, 编码=%s\n", videoStreamIndex,
                codecParameters->width, codecParameters->height,
                avcodec_get_name(codecParameters->codec_id));

    // ===== 阶段③：建解码器(AVCodec 算法本体 + AVCodecContext 工作环境)=====
    // 这正是 01 §4 的核心：codecpar 只是"描述",要解码得建一个有状态的解码器实例。
    // (1) 按编码 id 找到对应解码器(h264 → h264 解码器)。AVCodec 是无状态的算法本体,全局共享。
    const AVCodec *decoder = avcodec_find_decoder(codecParameters->codec_id);
    if (!decoder) {
        std::fprintf(stderr, "找不到对应解码器\n");
        avformat_close_input(&formatContext);
        return 1;
    }
    // (2) 分配解码器上下文(AVCodecContext,有状态:存宽高、像素格式、内部参考帧缓冲等)。
    AVCodecContext *codecContext = avcodec_alloc_context3(decoder);
    if (!codecContext) {
        std::fprintf(stderr, "分配解码器上下文失败\n");
        avformat_close_input(&formatContext);
        return 1;
    }
    // (3) 把流的参数(宽高/像素格式/extradata 即 SPS-PPS)从 codecpar 拷进上下文。
    if (avcodec_parameters_to_context(codecContext, codecParameters) < 0) {
        std::fprintf(stderr, "拷贝解码参数失败\n");
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        return 1;
    }
    // (4) 启动底层解码引擎。这就是 01 §4.3 "初始化三步" 的最后一步。
    if (avcodec_open2(codecContext, decoder, nullptr) < 0) {
        std::fprintf(stderr, "打开解码器失败\n");
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        return 1;
    }
    std::printf("✅ 解码器就绪: %s\n", decoder->name);

    // ===== 阶段④：解码循环(send_packet / receive_frame 异步状态机,见 01 §6.4)=====
    // 核心心智模型：发包和取帧是"解耦"的两条管道,不是 1 进 1 出。
    //   - 喂一个 packet 进去,可能一帧都取不出来(B 帧要等后续参考帧,解码器先攒着)；
    //   - 也可能一次取出好几帧。所以是"喂一个包 → 把当前能取的帧全取干净"的双层循环。
    // packet 装"压缩数据"(一个 NALU 序列),frame 装"解码后的原始像素"(这里是 YUV420P)。
    AVPacket *packet = av_packet_alloc();   // 容器复用：每轮读进数据、循环末尾 unref 释放引用
    AVFrame *frame = av_frame_alloc();      // 同理,receive 拿到帧、用完 unref
    if (!packet || !frame) {
        std::fprintf(stderr, "分配 packet/frame 失败\n");
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        return 1;
    }

    long decodedFrameCount = 0;
    bool firstFramePrinted = false;

    // ===== 阶段⑤ 的状态：YUV → RGB 转换器 + 目标 RGB 缓冲 =====
    // 解码器吐的是 YUV420P,SDL 纹理/显示要 RGB,中间靠 SwsContext 做像素格式转换(见 03)。
    // 懒创建：等拿到第一帧、知道确切宽高和像素格式后再建,比用 codecContext 的值更稳。
    SwsContext *swsContext = nullptr;
    uint8_t *rgbData[4] = {nullptr};  // 目标缓冲的 4 个平面指针(RGB 是 packed,只用 rgbData[0])
    int rgbLinesize[4] = {0};         // 每个平面的行字节数(含对齐填充,可能 > width*3)

    // 把一帧 RGB24 存成 PPM 图片(P6),纯为本步做"无窗口验证"用——下一步⑥接 SDL 后会删掉。
    // 注意按 stride(rgbLinesize[0]) 逐行写,不能一把 fwrite,否则会把行尾填充也写进去导致斜切。
    auto dumpFrameAsPPM = [](const char *path, uint8_t *rgb, int stride, int width, int height) {
        std::FILE *file = std::fopen(path, "wb");
        if (!file) return;
        std::fprintf(file, "P6\n%d %d\n255\n", width, height);
        for (int row = 0; row < height; ++row) {
            std::fwrite(rgb + (size_t)row * stride, 1, (size_t)width * 3, file);
        }
        std::fclose(file);
    };

    // 内层:把解码器当前攒着的帧全部取出来。封装成 lambda 复用——drain 阶段(末尾)也要调它。
    auto drainDecodedFrames = [&]() {
        while (true) {
            int receiveResult = avcodec_receive_frame(codecContext, frame);
            // EAGAIN：当前喂的数据还不够吐帧,回去再喂包；EOF：已 flush 完,彻底没帧了。
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
                break;
            }
            if (receiveResult < 0) {
                std::fprintf(stderr, "解码出错\n");
                break;
            }
            ++decodedFrameCount;

            // 第一帧打印关键事实,把"解出来到底是什么"看清楚:
            //   - format 是 YUV420P 而非 RGB —— 解码器吐的是 YUV,显示前还要 swscale 转(下一步⑤);
            //   - linesize[0] 往往 > width —— 每行末尾有对齐填充,逐行拷贝必须按 linesize 走(见 02 §linesize);
            //   - pts 是"显示时间戳",单位是 time_base,后面⑦控制节奏要用它换算成秒。
            if (!firstFramePrinted) {
                std::printf("首帧: format=%s, %dx%d, linesize[0]=%d, pts=%lld\n",
                            av_get_pix_fmt_name((AVPixelFormat)frame->format),
                            frame->width, frame->height, frame->linesize[0],
                            (long long)frame->pts);
                firstFramePrinted = true;
            }

            // 懒创建转换器:第一帧时按它的真实宽高/像素格式建好 SwsContext 和 RGB 目标缓冲。
            if (!swsContext) {
                swsContext = sws_getContext(
                    frame->width, frame->height, (AVPixelFormat)frame->format,  // 源:YUV420P
                    frame->width, frame->height, AV_PIX_FMT_RGB24,              // 目标:同尺寸 RGB24
                    SWS_BILINEAR, nullptr, nullptr, nullptr);
                // 按目标格式分配对齐缓冲;返回的 rgbLinesize[0] 才是真实行宽,后面拷贝都按它走。
                av_image_alloc(rgbData, rgbLinesize,
                               frame->width, frame->height, AV_PIX_FMT_RGB24, 1);
            }

            // 真正的转换:把 YUV 三平面喂进去,吐出一整块 RGB24。
            // 第 4/5 个参数 (0, frame->height) 表示"从源的第 0 行起、处理 height 行"(整帧)。
            sws_scale(swsContext,
                      frame->data, frame->linesize, 0, frame->height,
                      rgbData, rgbLinesize);

            // 本步验证:把第一帧 RGB 落盘成 PPM,跳出去用图片工具/ffprobe 看画面对不对。
            if (decodedFrameCount == 1) {
                dumpFrameAsPPM("first_frame.ppm", rgbData[0], rgbLinesize[0],
                               frame->width, frame->height);
                std::printf("已转 RGB 并导出首帧 → first_frame.ppm (stride=%d)\n", rgbLinesize[0]);
            }

            av_frame_unref(frame);  // 取完这帧,释放它持有的缓冲引用,容器留着下轮复用
        }
    };

    // 外层:不停从文件读 packet,只喂视频流的包给解码器。
    while (av_read_frame(formatContext, packet) >= 0) {
        if (packet->stream_index == videoStreamIndex) {
            if (avcodec_send_packet(codecContext, packet) >= 0) {
                drainDecodedFrames();
            }
        }
        av_packet_unref(packet);  // 不论是不是视频包,读进来的 packet 用完都要 unref(见 01 §5.2)
    }

    // drain(冲刷):文件读完了,但解码器内部可能还攒着几帧(尤其有 B 帧时)。
    // 喂一个 NULL 包告诉它"没有更多输入了",再把残余帧取干净(见 01 §6.4 drain)。
    avcodec_send_packet(codecContext, nullptr);
    drainDecodedFrames();

    std::printf("✅ 解码完成,共 %ld 帧\n", decodedFrameCount);

    // ===== 后续阶段(占位,逐步填) =====
    // ⑥ SDL 显示
    // ⑦ 节奏控制

    // ===== 清理(初始化建的,退出时 free / close,见 01 §5.6)=====
    sws_freeContext(swsContext);     // 转换器
    av_freep(&rgbData[0]);           // av_image_alloc 分配的 RGB 缓冲(只 free 第 0 个指针)
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    return 0;
}
