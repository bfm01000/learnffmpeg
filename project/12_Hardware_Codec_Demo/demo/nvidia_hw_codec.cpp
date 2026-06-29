// ============================================================================
// nvidia_hw_codec.cpp — NVIDIA CUDA 硬件编解码全链路演示
//
// 运行环境: Linux + NVIDIA GPU + FFmpeg (带 CUDA/NVENC/NVDEC 支持)
//
// 验证 FFmpeg 是否支持 CUDA:
//   ffmpeg -hwaccels | grep cuda          # 应显示 cuda
//   ffmpeg -codecs | grep cuvid           # 应显示 h264_cuvid / hevc_cuvid
//   ffmpeg -codecs | grep nvenc           # 应显示 h264_nvenc / hevc_nvenc
//
// 本 Demo 演示四个模式:
//   MODE 1 (--decode):       硬件解码，帧留在 GPU 显存
//   MODE 2 (--encode):       硬件编码 (NVENC)
//   MODE 3 (--transcode):    全 GPU 转码 (CUDA 解码→scale_cuda 缩放→NVENC 编码)
//   MODE 4 (--wrong-way):    错误示范——硬解后拉回 CPU 再软编（性能对比）
//
// 核心学习目标:
//   1. av_hwdevice_ctx_create / av_hwframe_ctx_alloc / av_hwframe_ctx_init 三连
//   2. 硬件帧 AV_PIX_FMT_CUDA vs 软件帧 AV_PIX_FMT_YUV420P 的内存模型差异
//   3. av_hwframe_transfer_data 是 PCIe 数据搬运——热路径避开
//   4. scale_cuda 硬件滤镜——GPU 上做缩放，不下 CPU
//
// 三平台对比:
//   本文件是 NVIDIA(CUDA) 的实现。Android→demo/android_hw_codec.cpp,
//   Apple→demo/apple_hw_codec.cpp。三个文件的函数命名和结构是对齐的，
//   方便对照学习三平台的 API 差异。
//
// 参考: Doc/ffmpeg/07-硬件编解码.md §5.2, §5.5.6 Q5
// ============================================================================

extern "C" {
// ── FFmpeg 核心头文件 ──
#include <libavcodec/avcodec.h>       // 编解码器 API
#include <libavformat/avformat.h>     // 解封装/封装 API
#include <libavutil/avutil.h>         // AV_PIX_FMT_*, av_log
#include <libavutil/hwcontext.h>      // ★ av_hwdevice_ctx_create, AVHWFramesContext, AV_HWDEVICE_TYPE_CUDA
#include <libavutil/opt.h>            // av_opt_set — 设置编码器参数
#include <libavutil/pixdesc.h>        // av_get_pix_fmt_name
#include <libavfilter/avfilter.h>     // ★ 滤镜图: scale_cuda
#include <libavfilter/buffersrc.h>    // av_buffersrc_add_frame
#include <libavfilter/buffersink.h>   // av_buffersink_get_frame
#include <libswscale/swscale.h>       // sws_scale (仅在 MODE 4 做对比时用)
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/time.h>

// ============================================================
// 工具函数
// ============================================================

// 获取当前时间(毫秒) —— 用于性能对比
static int64_t now_ms() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// FFmpeg 错误码 → 可读字符串
static const char* errstr(int err) {
    static thread_local char buf[256];
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

// 打印 AVFrame 的关键信息: 格式、宽高、是否硬件帧
static void print_frame_info(const char* tag, AVFrame* frame) {
    const char* fmt_name = av_get_pix_fmt_name((AVPixelFormat)frame->format);
    bool is_hw = (frame->format == AV_PIX_FMT_CUDA);
    printf("  [%s] fmt=%s(%s) %dx%d pts=%ld\n",
           tag,
           fmt_name ? fmt_name : "unknown",
           is_hw ? "GPU_CUDA" : "CPU",
           frame->width, frame->height, frame->pts);
}

// ============================================================
// 硬件设备/帧上下文创建 —— 三平台差异的核心
//
// 这是 NVIDIA 的版本。Android 和 Apple 的差异见:
//   Android: demo/android_hw_codec.cpp → AMediaCodec_createDecoderByType
//   Apple:   demo/apple_hw_codec.cpp   → av_hwdevice_ctx_create(AV_HWDEVICE_TYPE_VIDEOTOOLBOX)
// ============================================================

// 创建 CUDA 硬件设备上下文 —— 本质是"打开 GPU 连接"
static AVBufferRef* create_cuda_device() {
    AVBufferRef* hw_ctx = nullptr;
    int ret = av_hwdevice_ctx_create(&hw_ctx, AV_HWDEVICE_TYPE_CUDA,
                                      nullptr, nullptr, 0);
    if (ret < 0) {
        fprintf(stderr, "ERROR: Failed to create CUDA device: %s\n", errstr(ret));
        fprintf(stderr, "  Check: nvidia-smi, and FFmpeg built with --enable-cuda\n");
        exit(1);
    }
    printf("[CUDA] Hardware device context created (AV_HWDEVICE_TYPE_CUDA)\n");
    return hw_ctx;
}

// 创建 CUDA 硬件帧池 —— 解码后的帧从显存分配
// frames_ctx->format = AV_PIX_FMT_CUDA   → 帧留在 GPU
// frames_ctx->sw_format = AV_PIX_FMT_NV12 → 如果被迫拷回 CPU，用什么格式
static AVBufferRef* create_cuda_frames_ctx(AVBufferRef* hw_device_ctx,
                                            int width, int height) {
    AVBufferRef* hw_frames_ctx = av_hwframe_ctx_alloc(hw_device_ctx);
    if (!hw_frames_ctx) {
        fprintf(stderr, "ERROR: av_hwframe_ctx_alloc failed\n");
        exit(1);
    }

    AVHWFramesContext* fctx = (AVHWFramesContext*)hw_frames_ctx->data;
    fctx->format    = AV_PIX_FMT_CUDA;      // ★ 核心: 帧格式设为 CUDA
    fctx->sw_format = AV_PIX_FMT_NV12;      // ★ 必填: 万一回 CPU 用 NV12
    fctx->width     = width;
    fctx->height    = height;
    fctx->initial_pool_size = 20;           // 池大小，解码用 20 足够

    int ret = av_hwframe_ctx_init(hw_frames_ctx);
    if (ret < 0) {
        fprintf(stderr, "ERROR: av_hwframe_ctx_init: %s\n", errstr(ret));
        exit(1);
    }
    printf("[CUDA] Hardware frames context: %dx%d, format=CUDA, sw_format=NV12, pool=20\n",
           width, height);
    return hw_frames_ctx;
}

// ============================================================
// MODE 1: 硬件解码 —— 帧留在 GPU，只打印信息
//
// 路径: 输入文件 → av_read_frame → avcodec_send_packet
//       → avcodec_receive_frame → AV_PIX_FMT_CUDA 帧 (在显存)
//       → 打印帧信息 (不下 CPU)
//
// ★ 和三平台的区别:
//   NVIDIA: av_hwdevice_ctx_create(AV_HWDEVICE_TYPE_CUDA) + hw_frames_ctx
//   Android: AMediaCodec + Surface 输出 (见 android_hw_codec.cpp)
//   Apple:   av_hwdevice_ctx_create(AV_HWDEVICE_TYPE_VIDEOTOOLBOX) (见 apple_hw_codec.cpp)
// ============================================================
static void mode_decode(const char* input_file) {
    printf("\n========== MODE 1: CUDA Hardware Decode ==========\n");

    // ── 1. 打开输入文件 ──
    AVFormatContext* fmt_ctx = nullptr;
    int ret = avformat_open_input(&fmt_ctx, input_file, nullptr, nullptr);
    if (ret < 0) { fprintf(stderr, "ERROR: avformat_open_input: %s\n", errstr(ret)); exit(1); }
    avformat_find_stream_info(fmt_ctx, nullptr);

    // ── 2. 找到视频流 ──
    int video_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_idx < 0) { fprintf(stderr, "ERROR: no video stream\n"); exit(1); }

    AVCodecParameters* par = fmt_ctx->streams[video_idx]->codecpar;
    printf("[Input] %s, codec=%s, %dx%d\n",
           input_file, avcodec_get_name(par->codec_id), par->width, par->height);

    // ── 3. 打开解码器 ──
    const AVCodec* decoder = avcodec_find_decoder(par->codec_id);
    if (!decoder) { fprintf(stderr, "ERROR: decoder not found\n"); exit(1); }

    AVCodecContext* codec_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(codec_ctx, par);

    // ── 4. ★ 核心: 创建 CUDA 硬件上下文并挂到解码器 ──
    //        这是 "-hwaccel cuda -hwaccel_output_format cuda" 的 API 等价
    AVBufferRef* hw_device_ctx = create_cuda_device();
    AVBufferRef* hw_frames_ctx = create_cuda_frames_ctx(
        hw_device_ctx, par->width, par->height);

    // 两种挂法:
    //   方法 A (固定池): codec_ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ctx)
    //      → 解码帧从我们建的池分配，可控制 sw_format 和池大小 (推荐)
    //   方法 B (动态):   codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx)
    //      → 解码器内部自己建池，代码更短但控制力弱
    codec_ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ctx);  // 方法 A

    ret = avcodec_open2(codec_ctx, decoder, nullptr);
    if (ret < 0) { fprintf(stderr, "ERROR: avcodec_open2: %s\n", errstr(ret)); exit(1); }

    // ── 5. 解码循环 ──
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    int frame_count = 0;
    int64_t t0 = now_ms();

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index != video_idx) { av_packet_unref(pkt); continue; }

        ret = avcodec_send_packet(codec_ctx, pkt);
        if (ret < 0) { fprintf(stderr, "ERROR: send_packet: %s\n", errstr(ret)); break; }

        while (avcodec_receive_frame(codec_ctx, frame) == 0) {
            print_frame_info("DECODED", frame);
            // ★ frame->format == AV_PIX_FMT_CUDA
            //   frame->data[0] 是 CUdeviceptr (设备指针)
            //   如果在这里调 av_hwframe_transfer_data，帧就被拷回 CPU
            //   正确做法: 直接消费——喂给 CUDA kernel / OpenGL interop / 硬件编码器
            frame_count++;
        }
        av_packet_unref(pkt);
    }
    // flush decoder
    avcodec_send_packet(codec_ctx, nullptr);
    while (avcodec_receive_frame(codec_ctx, frame) == 0) {
        print_frame_info("DECODED(flush)", frame);
        frame_count++;
    }

    int64_t elapsed = now_ms() - t0;
    printf("\n[MODE 1] Decoded %d frames in %ld ms (%.1f fps), all on GPU\n",
           frame_count, elapsed,
           elapsed > 0 ? frame_count * 1000.0 / elapsed : 0);

    // ── 6. 清理 ──
    av_frame_free(&frame);
    av_packet_free(&pkt);
    av_buffer_unref(&hw_frames_ctx);
    av_buffer_unref(&hw_device_ctx);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
}

// ============================================================
// MODE 2: 硬件编码 (NVENC)
//
// 路径: 生成测试 YUV 帧 (CPU) → av_hwframe_transfer_data (上传到 GPU)
//       → avcodec_send_frame → avcodec_receive_packet → 写入文件
//
// ★ 注意: 纯编码场景，输入是 CPU YUV 帧，需要上传到 GPU
//         但如果是"解码→编码"的转码场景，输入已经是 GPU 帧，不需要上传
// ============================================================
static void mode_encode(const char* output_file, int width, int height, int frame_count) {
    printf("\n========== MODE 2: NVENC Hardware Encode ==========\n");
    printf("[Encode] Output: %s, %dx%d, %d frames\n",
           output_file, width, height, frame_count);

    // ── 1. 打开 NVENC 编码器 ──
    const AVCodec* encoder = avcodec_find_encoder_by_name("h264_nvenc");
    if (!encoder) {
        fprintf(stderr, "ERROR: h264_nvenc not found\n");
        fprintf(stderr, "  FFmpeg must be built with --enable-nvenc\n");
        exit(1);
    }

    AVCodecContext* enc_ctx = avcodec_alloc_context3(encoder);
    enc_ctx->width     = width;
    enc_ctx->height    = height;
    enc_ctx->time_base = (AVRational){1, 30};
    enc_ctx->framerate = (AVRational){30, 1};
    enc_ctx->pix_fmt   = AV_PIX_FMT_CUDA;  // ★ 输入是 GPU 帧
    enc_ctx->bit_rate  = 4000000;           // 4 Mbps
    enc_ctx->gop_size  = 30;
    enc_ctx->max_b_frames = 0;             // 低延迟

    // NVENC 特有参数——和 CLI 的 -preset p4 对应
    av_opt_set(enc_ctx->priv_data, "preset", "p4", 0);   // p1~p7: p1 最快 p7 最慢
    av_opt_set(enc_ctx->priv_data, "tune", "ll", 0);     // low latency
    av_opt_set(enc_ctx->priv_data, "rc", "vbr", 0);      // 可变码率

    // ── 2. 创建 CUDA 硬件帧上下文(编码侧) ──
    AVBufferRef* hw_device_ctx = create_cuda_device();
    AVBufferRef* hw_frames_ctx = create_cuda_frames_ctx(hw_device_ctx, width, height);
    enc_ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ctx);

    int ret = avcodec_open2(enc_ctx, encoder, nullptr);
    if (ret < 0) { fprintf(stderr, "ERROR: avcodec_open2(enc): %s\n", errstr(ret)); exit(1); }

    // ── 3. 创建输出文件 ──
    AVFormatContext* out_fmt = nullptr;
    avformat_alloc_output_context2(&out_fmt, nullptr, nullptr, output_file);
    if (!out_fmt) {
        avformat_alloc_output_context2(&out_fmt, nullptr, "mp4", output_file);
    }
    AVStream* out_stream = avformat_new_stream(out_fmt, nullptr);
    avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
    out_stream->time_base = enc_ctx->time_base;

    ret = avio_open(&out_fmt->pb, output_file, AVIO_FLAG_WRITE);
    if (ret < 0) { fprintf(stderr, "ERROR: avio_open: %s\n", errstr(ret)); exit(1); }
    avformat_write_header(out_fmt, nullptr);

    // ── 4. 生成测试帧 → 上传到 GPU → 编码 ──
    //     CPU 帧: libswscale 生成彩色渐变
    AVFrame* sw_frame = av_frame_alloc();
    sw_frame->format = AV_PIX_FMT_NV12;
    sw_frame->width  = width;
    sw_frame->height = height;
    av_frame_get_buffer(sw_frame, 0);

    AVFrame* hw_frame = av_frame_alloc();  // GPU 帧
    AVPacket* enc_pkt = av_packet_alloc();

    int64_t t0 = now_ms();

    for (int i = 0; i < frame_count; i++) {
        // 4a. 生成 CPU YUV 测试画面 (彩色条)
        // Y 平面: 从左到右渐变
        for (int y = 0; y < height; y++) {
            uint8_t* row = sw_frame->data[0] + y * sw_frame->linesize[0];
            memset(row, (i * 5 + y * 255 / height) % 256, width);
        }
        // UV 平面: 彩色
        for (int y = 0; y < height / 2; y++) {
            uint8_t* row = sw_frame->data[1] + y * sw_frame->linesize[1];
            for (int x = 0; x < width / 2; x++) {
                row[x * 2]     = (i * 3 + x * 255 / width) % 256;      // U
                row[x * 2 + 1] = (128 + i * 2) % 256;                   // V
            }
        }
        sw_frame->pts = i;

        // 4b. ★ 关键: CPU→GPU 上传 — av_hwframe_transfer_data
        //       方向: sw_frame(CPU) → hw_frame(GPU)
        ret = av_hwframe_get_buffer(hw_frames_ctx, hw_frame, 0);
        if (ret < 0) { fprintf(stderr, "ERROR: get_buffer: %s\n", errstr(ret)); exit(1); }
        ret = av_hwframe_transfer_data(hw_frame, sw_frame, 0);
        if (ret < 0) { fprintf(stderr, "ERROR: transfer CPU→GPU: %s\n", errstr(ret)); exit(1); }

        // 4c. 编码
        ret = avcodec_send_frame(enc_ctx, hw_frame);
        if (ret < 0) { fprintf(stderr, "ERROR: send_frame: %s\n", errstr(ret)); exit(1); }

        while (avcodec_receive_packet(enc_ctx, enc_pkt) == 0) {
            av_packet_rescale_ts(enc_pkt, enc_ctx->time_base, out_stream->time_base);
            enc_pkt->stream_index = out_stream->index;
            av_interleaved_write_frame(out_fmt, enc_pkt);
            av_packet_unref(enc_pkt);
        }
        av_frame_unref(hw_frame);
    }
    // flush
    avcodec_send_frame(enc_ctx, nullptr);
    while (avcodec_receive_packet(enc_ctx, enc_pkt) == 0) {
        av_packet_rescale_ts(enc_pkt, enc_ctx->time_base, out_stream->time_base);
        enc_pkt->stream_index = out_stream->index;
        av_interleaved_write_frame(out_fmt, enc_pkt);
        av_packet_unref(enc_pkt);
    }

    int64_t elapsed = now_ms() - t0;
    printf("\n[MODE 2] Encoded %d frames in %ld ms (%.1f fps)\n",
           frame_count, elapsed,
           elapsed > 0 ? frame_count * 1000.0 / elapsed : 0);

    // ── 5. 清理 ──
    av_write_trailer(out_fmt);
    avio_closep(&out_fmt->pb);
    av_frame_free(&sw_frame);
    av_frame_free(&hw_frame);
    av_packet_free(&enc_pkt);
    av_buffer_unref(&hw_frames_ctx);
    av_buffer_unref(&hw_device_ctx);
    avcodec_free_context(&enc_ctx);
    avformat_free_context(out_fmt);
    printf("[MODE 2] Output: %s\n", output_file);
}

// ============================================================
// MODE 3: 全 GPU 转码 (硬件解码→GPU 缩放→硬件编码)
//
// 路径: 输入文件 → CUDA 解码 (GPU帧) → scale_cuda 滤镜 (GPU缩放)
//       → NVENC 编码 (GPU帧输入) → 写入输出文件
//
// ★ 全程数据不下 CPU，PCIe 零读写——这是生产环境的标准链路
// ★ 和 MODE 4 对比着看: MODE 4 是"错的"——硬解→回 CPU→软编
//
// 三平台等价链路:
//   NVIDIA: AV_HWDEVICE_TYPE_CUDA  + scale_cuda   + h264_nvenc
//   Apple:  AV_HWDEVICE_TYPE_VIDEOTOOLBOX + scale_vt + h264_videotoolbox
//   Android: 不走 FFmpeg，MediaCodec Surface→Surface (见 android_hw_codec.cpp)
// ============================================================
static void mode_transcode(const char* input_file, const char* output_file,
                           int out_width, int out_height) {
    printf("\n========== MODE 3: Full-GPU Transcode ==========\n");
    printf("[Transcode] %s → %s (%dx%d)\n", input_file, output_file, out_width, out_height);

    // ── 1. 打开输入 ──
    AVFormatContext* in_fmt = nullptr;
    int ret = avformat_open_input(&in_fmt, input_file, nullptr, nullptr);
    if (ret < 0) { fprintf(stderr, "ERROR: avformat_open_input: %s\n", errstr(ret)); exit(1); }
    avformat_find_stream_info(in_fmt, nullptr);

    int video_idx = av_find_best_stream(in_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    AVCodecParameters* in_par = in_fmt->streams[video_idx]->codecpar;
    AVRational in_tb = in_fmt->streams[video_idx]->time_base;

    // ── 2. 打开 CUDA 解码器 ──
    const AVCodec* decoder = avcodec_find_decoder(in_par->codec_id);
    AVCodecContext* dec_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(dec_ctx, in_par);

    AVBufferRef* hw_device = create_cuda_device();
    AVBufferRef* hw_frames_in = create_cuda_frames_ctx(
        hw_device, in_par->width, in_par->height);
    dec_ctx->hw_frames_ctx = av_buffer_ref(hw_frames_in);
    avcodec_open2(dec_ctx, decoder, nullptr);

    // ── 3. 创建 scale_cuda 硬件滤镜图 ──
    //     路径: buffer(in) → scale_cuda → buffersink(out)
    char args[512];
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=1/1",
             dec_ctx->width, dec_ctx->height, AV_PIX_FMT_CUDA,
             in_tb.num, in_tb.den);

    AVFilterGraph* filter_graph = avfilter_graph_alloc();
    const AVFilter* buffersrc  = avfilter_get_by_name("buffer");
    const AVFilter* buffersink = avfilter_get_by_name("buffersink");

    AVFilterContext* src_ctx  = nullptr;
    AVFilterContext* sink_ctx = nullptr;
    avfilter_graph_create_filter(&src_ctx, buffersrc, "in", args, nullptr, filter_graph);
    avfilter_graph_create_filter(&sink_ctx, buffersink, "out", nullptr, nullptr, filter_graph);

    // scale_cuda —— GPU 上的缩放
    const AVFilter* scale_filter = avfilter_get_by_name("scale_cuda");
    if (!scale_filter) {
        fprintf(stderr, "ERROR: scale_cuda filter not found\n");
        fprintf(stderr, "  FFmpeg must be built with --enable-libnpp or --enable-cuda-nvcc\n");
        exit(1);
    }
    char scale_args[128];
    snprintf(scale_args, sizeof(scale_args), "w=%d:h=%d", out_width, out_height);
    AVFilterContext* scale_ctx = nullptr;
    avfilter_graph_create_filter(&scale_ctx, scale_filter, "scale",
                                  scale_args, nullptr, filter_graph);

    // 连接: buffer → scale_cuda → buffersink
    avfilter_link(src_ctx, 0, scale_ctx, 0);
    avfilter_link(scale_ctx, 0, sink_ctx, 0);
    avfilter_graph_config(filter_graph, nullptr);

    // ── 4. 创建 NVENC 编码器 + 输出文件 ──
    AVBufferRef* hw_frames_out = create_cuda_frames_ctx(
        hw_device, out_width, out_height);

    const AVCodec* encoder = avcodec_find_encoder_by_name("h264_nvenc");
    AVCodecContext* enc_ctx = avcodec_alloc_context3(encoder);
    enc_ctx->width     = out_width;
    enc_ctx->height    = out_height;
    enc_ctx->time_base = (AVRational){1, 30};
    enc_ctx->framerate = (AVRational){30, 1};
    enc_ctx->pix_fmt   = AV_PIX_FMT_CUDA;
    enc_ctx->bit_rate  = 3000000;  // 3 Mbps (缩放后分辨率更低，码率也降)
    enc_ctx->gop_size  = 30;
    enc_ctx->max_b_frames = 0;
    enc_ctx->hw_frames_ctx = av_buffer_ref(hw_frames_out);
    av_opt_set(enc_ctx->priv_data, "preset", "p4", 0);
    av_opt_set(enc_ctx->priv_data, "tune", "ll", 0);
    avcodec_open2(enc_ctx, encoder, nullptr);

    AVFormatContext* out_fmt = nullptr;
    avformat_alloc_output_context2(&out_fmt, nullptr, nullptr, output_file);
    if (!out_fmt) avformat_alloc_output_context2(&out_fmt, nullptr, "mp4", output_file);
    AVStream* out_stream = avformat_new_stream(out_fmt, nullptr);
    avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
    out_stream->time_base = enc_ctx->time_base;
    avio_open(&out_fmt->pb, output_file, AVIO_FLAG_WRITE);
    avformat_write_header(out_fmt, nullptr);

    // ── 5. 主循环: 解码 → scale_cuda → 编码 ──
    AVPacket* pkt = av_packet_alloc();
    AVFrame* dec_frame = av_frame_alloc();  // 解码输出 (GPU)
    AVFrame* filt_frame = av_frame_alloc(); // 滤镜输出 (GPU)
    AVPacket* enc_pkt = av_packet_alloc();
    int frame_count = 0;
    int64_t t0 = now_ms();

    while (av_read_frame(in_fmt, pkt) >= 0) {
        if (pkt->stream_index != video_idx) { av_packet_unref(pkt); continue; }

        avcodec_send_packet(dec_ctx, pkt);
        while (avcodec_receive_frame(dec_ctx, dec_frame) == 0) {
            // 5a. 硬件解码帧 → 喂给 scale_cuda 滤镜
            ret = av_buffersrc_add_frame(src_ctx, dec_frame);
            if (ret < 0) { fprintf(stderr, "ERROR: add_frame: %s\n", errstr(ret)); exit(1); }

            // 5b. 从滤镜取缩放后的帧 (仍是 GPU 帧)
            while (av_buffersink_get_frame(sink_ctx, filt_frame) == 0) {
                // 5c. 硬件缩放帧 → 喂给 NVENC 编码器
                ret = avcodec_send_frame(enc_ctx, filt_frame);
                if (ret < 0) { fprintf(stderr, "ERROR: send_frame enc: %s\n", errstr(ret)); exit(1); }

                while (avcodec_receive_packet(enc_ctx, enc_pkt) == 0) {
                    av_packet_rescale_ts(enc_pkt, enc_ctx->time_base, out_stream->time_base);
                    enc_pkt->stream_index = out_stream->index;
                    av_interleaved_write_frame(out_fmt, enc_pkt);
                    av_packet_unref(enc_pkt);
                }
                av_frame_unref(filt_frame);
                frame_count++;
            }
        }
        av_packet_unref(pkt);
    }
    // flush decoder
    avcodec_send_packet(dec_ctx, nullptr);
    while (avcodec_receive_frame(dec_ctx, dec_frame) == 0) {
        av_buffersrc_add_frame(src_ctx, dec_frame);
        while (av_buffersink_get_frame(sink_ctx, filt_frame) == 0) {
            avcodec_send_frame(enc_ctx, filt_frame);
            while (avcodec_receive_packet(enc_ctx, enc_pkt) == 0) {
                av_packet_rescale_ts(enc_pkt, enc_ctx->time_base, out_stream->time_base);
                enc_pkt->stream_index = out_stream->index;
                av_interleaved_write_frame(out_fmt, enc_pkt);
                av_packet_unref(enc_pkt);
            }
            av_frame_unref(filt_frame);
            frame_count++;
        }
    }
    // flush encoder
    avcodec_send_frame(enc_ctx, nullptr);
    while (avcodec_receive_packet(enc_ctx, enc_pkt) == 0) {
        av_packet_rescale_ts(enc_pkt, enc_ctx->time_base, out_stream->time_base);
        enc_pkt->stream_index = out_stream->index;
        av_interleaved_write_frame(out_fmt, enc_pkt);
        av_packet_unref(enc_pkt);
    }

    int64_t elapsed = now_ms() - t0;
    printf("\n[MODE 3] Transcoded %d frames in %ld ms (%.1f fps, all GPU)\n",
           frame_count, elapsed,
           elapsed > 0 ? frame_count * 1000.0 / elapsed : 0);

    // ── 6. 清理 ──
    av_write_trailer(out_fmt);
    avio_closep(&out_fmt->pb);
    av_frame_free(&dec_frame);
    av_frame_free(&filt_frame);
    av_packet_free(&pkt);
    av_packet_free(&enc_pkt);
    avfilter_graph_free(&filter_graph);
    av_buffer_unref(&hw_frames_in);
    av_buffer_unref(&hw_frames_out);
    av_buffer_unref(&hw_device);
    avcodec_free_context(&dec_ctx);
    avcodec_free_context(&enc_ctx);
    avformat_free_context(out_fmt);
    avformat_close_input(&in_fmt);
    printf("[MODE 3] Output: %s\n", output_file);
}

// ============================================================
// MODE 4: ★ 错误示范 —— 硬解后拉回 CPU 再软编
//
// 路径: 输入 → CUDA 解码 (GPU帧)
//       → av_hwframe_transfer_data (GPU→CPU, PCIe 搬运) ★ 耗时
//       → libx264 编码 (CPU) ★ 耗 CPU
//       → 写入输出文件
//
// 作用: 和 MODE 3 对比，直观感受 CPU→GPU 搬运和软编的额外开销
//
// ★ 这条路径正是 07-硬件编解码.md 里反复强调"不要做"的事情
//   “桌面上这就是穿越 PCIe 的那一下，热路径避开”
// ============================================================
static void mode_wrong_way(const char* input_file, const char* output_file) {
    printf("\n========== MODE 4: WRONG WAY (GPU decode → CPU readback → CPU encode) ==========\n");
    printf("[WrongWay] %s → %s\n", input_file, output_file);
    printf("  WARNING: This is the ANTI-PATTERN — PCIe readback + software encode.\n");
    printf("  Compare performance with MODE 3.\n\n");

    // ── 1. 输入 + CUDA 解码器 (同 MODE 1/3) ──
    AVFormatContext* in_fmt = nullptr;
    avformat_open_input(&in_fmt, input_file, nullptr, nullptr);
    avformat_find_stream_info(in_fmt, nullptr);
    int video_idx = av_find_best_stream(in_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    AVCodecParameters* in_par = in_fmt->streams[video_idx]->codecpar;

    const AVCodec* decoder = avcodec_find_decoder(in_par->codec_id);
    AVCodecContext* dec_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(dec_ctx, in_par);
    AVBufferRef* hw_device = create_cuda_device();
    AVBufferRef* hw_frames = create_cuda_frames_ctx(hw_device, in_par->width, in_par->height);
    dec_ctx->hw_frames_ctx = av_buffer_ref(hw_frames);
    avcodec_open2(dec_ctx, decoder, nullptr);

    // ── 2. CPU 软编码器 (libx264) ──
    const AVCodec* encoder = avcodec_find_encoder_by_name("libx264");
    if (!encoder) { fprintf(stderr, "ERROR: libx264 not found\n"); exit(1); }
    AVCodecContext* enc_ctx = avcodec_alloc_context3(encoder);
    enc_ctx->width     = in_par->width;
    enc_ctx->height    = in_par->height;
    enc_ctx->time_base = (AVRational){1, 30};
    enc_ctx->framerate = (AVRational){30, 1};
    enc_ctx->pix_fmt   = AV_PIX_FMT_YUV420P;  // ★ 软件编码，输入是 CPU 帧
    enc_ctx->bit_rate  = 4000000;
    enc_ctx->gop_size  = 30;
    av_opt_set(enc_ctx->priv_data, "preset", "medium", 0);
    avcodec_open2(enc_ctx, encoder, nullptr);

    // ── 3. 输出文件 ──
    AVFormatContext* out_fmt = nullptr;
    avformat_alloc_output_context2(&out_fmt, nullptr, nullptr, output_file);
    if (!out_fmt) avformat_alloc_output_context2(&out_fmt, nullptr, "mp4", output_file);
    AVStream* out_stream = avformat_new_stream(out_fmt, nullptr);
    avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
    out_stream->time_base = enc_ctx->time_base;
    avio_open(&out_fmt->pb, output_file, AVIO_FLAG_WRITE);
    avformat_write_header(out_fmt, nullptr);

    // ── 4. 主循环 ──
    int ret;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* hw_frame = av_frame_alloc();  // GPU 解码帧
    AVFrame* sw_frame = av_frame_alloc();  // CPU 帧 (GPU→CPU 拷贝的接收方)
    AVPacket* enc_pkt = av_packet_alloc();
    int frame_count = 0;
    int64_t t0 = now_ms();

    while (av_read_frame(in_fmt, pkt) >= 0) {
        if (pkt->stream_index != video_idx) { av_packet_unref(pkt); continue; }

        avcodec_send_packet(dec_ctx, pkt);
        while (avcodec_receive_frame(dec_ctx, hw_frame) == 0) {
            // ★★★ 关键步骤: GPU → CPU 拷贝 ★★★
            //   av_hwframe_transfer_data 内部:
            //   1. 如果 GPU 帧是 tiled 排布，GPU 驱动先做 deswizzle → 线性
            //   2. 通过 PCIe 总线 DMA → 系统内存
            //   3. 等待 DMA 完成 (同步点)
            //   这是全链路最大的性能杀手——和桌面 doc 07 §5.5.1 所述一致

            sw_frame->format = AV_PIX_FMT_NV12;  // CUDA 解码输出是 NV12
            sw_frame->width  = hw_frame->width;
            sw_frame->height = hw_frame->height;
            av_frame_get_buffer(sw_frame, 0);

            ret = av_hwframe_transfer_data(sw_frame, hw_frame, 0);  // ★ GPU→CPU, 穿过 PCIe
            if (ret < 0) { fprintf(stderr, "ERROR: transfer_data: %s\n", errstr(ret)); exit(1); }
            sw_frame->pts = hw_frame->pts;

            // 喂给 CPU 编码器
            avcodec_send_frame(enc_ctx, sw_frame);
            while (avcodec_receive_packet(enc_ctx, enc_pkt) == 0) {
                av_packet_rescale_ts(enc_pkt, enc_ctx->time_base, out_stream->time_base);
                enc_pkt->stream_index = out_stream->index;
                av_interleaved_write_frame(out_fmt, enc_pkt);
                av_packet_unref(enc_pkt);
            }
            av_frame_unref(hw_frame);
            av_frame_unref(sw_frame);
            frame_count++;
        }
        av_packet_unref(pkt);
    }
    // flush
    avcodec_send_packet(dec_ctx, nullptr);
    while (avcodec_receive_frame(dec_ctx, hw_frame) == 0) {
        sw_frame->format = AV_PIX_FMT_NV12;
        sw_frame->width = hw_frame->width;
        sw_frame->height = hw_frame->height;
        av_frame_get_buffer(sw_frame, 0);
        av_hwframe_transfer_data(sw_frame, hw_frame, 0);
        sw_frame->pts = hw_frame->pts;
        avcodec_send_frame(enc_ctx, sw_frame);
        while (avcodec_receive_packet(enc_ctx, enc_pkt) == 0) {
            av_packet_rescale_ts(enc_pkt, enc_ctx->time_base, out_stream->time_base);
            enc_pkt->stream_index = out_stream->index;
            av_interleaved_write_frame(out_fmt, enc_pkt);
            av_packet_unref(enc_pkt);
        }
        av_frame_unref(hw_frame);
        av_frame_unref(sw_frame);
        frame_count++;
    }
    avcodec_send_frame(enc_ctx, nullptr);
    while (avcodec_receive_packet(enc_ctx, enc_pkt) == 0) {
        av_packet_rescale_ts(enc_pkt, enc_ctx->time_base, out_stream->time_base);
        enc_pkt->stream_index = out_stream->index;
        av_interleaved_write_frame(out_fmt, enc_pkt);
        av_packet_unref(enc_pkt);
    }

    int64_t elapsed = now_ms() - t0;
    printf("\n[MODE 4] Encoded %d frames in %ld ms (%.1f fps, GPU→CPU→CPU encode)\n",
           frame_count, elapsed,
           elapsed > 0 ? frame_count * 1000.0 / elapsed : 0);
    printf("  Compare with MODE 3: MODE 3 does decode+scale+encode all on GPU.\n");
    printf("  MODE 4 pays: PCIe readback + libx264 CPU time.\n");

    // ── 5. 清理 ──
    av_write_trailer(out_fmt);
    avio_closep(&out_fmt->pb);
    av_frame_free(&hw_frame);
    av_frame_free(&sw_frame);
    av_packet_free(&pkt);
    av_packet_free(&enc_pkt);
    av_buffer_unref(&hw_frames);
    av_buffer_unref(&hw_device);
    avcodec_free_context(&dec_ctx);
    avcodec_free_context(&enc_ctx);
    avformat_free_context(out_fmt);
    avformat_close_input(&in_fmt);
    printf("[MODE 4] Output: %s\n", output_file);
}

// ============================================================
// main() —— 命令行入口
//
// 用法:
//   ./nvidia_hw_codec <input.mp4>
//     → 运行所有 4 个模式
//
//   ./nvidia_hw_codec <input.mp4> decode
//     → 只运行 MODE 1 (硬件解码)
//
//   ./nvidia_hw_codec <input.mp4> transcode
//     → 运行 MODE 3+4 (全 GPU 转码 vs 错误示范 对比)
// ============================================================
int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <input.mp4> [mode]\n", argv[0]);
        printf("  mode: decode | encode | transcode | all (default)\n");
        printf("\n");
        printf("Examples:\n");
        printf("  %s test.mp4              # run all modes\n", argv[0]);
        printf("  %s test.mp4 decode       # hardware decode only\n", argv[0]);
        printf("  %s test.mp4 transcode    # GPU transcode + wrong way comparison\n", argv[0]);
        return 1;
    }

    const char* input  = argv[1];
    const char* mode   = argc >= 3 ? argv[2] : "all";

    printf("========================================================\n");
    printf(" NVIDIA CUDA Hardware Codec Demo\n");
    printf(" Input: %s\n", input);
    printf("========================================================\n");

    if (strcmp(mode, "decode") == 0 || strcmp(mode, "all") == 0) {
        mode_decode(input);
    }

    if (strcmp(mode, "encode") == 0 || strcmp(mode, "all") == 0) {
        // 生成一个 3 秒 30fps 1080p 的编码输出
        mode_encode("output_nvenc.mp4", 1920, 1080, 90);
    }

    if (strcmp(mode, "transcode") == 0 || strcmp(mode, "all") == 0) {
        // GPU 转码: 输入 → 缩小到 720p → 输出
        mode_transcode(input, "output_gpu_transcode.mp4", 1280, 720);

        // 错误示范: 硬解 → 拷回 CPU → 软编
        printf("\n--- Comparison: MODE 3 (GPU) vs MODE 4 (GPU→CPU→CPU) ---\n");
        mode_wrong_way(input, "output_wrong_way.mp4");
    }

    printf("\n========================================================\n");
    printf(" Done. Output files:\n");
    if (strcmp(mode, "encode") == 0 || strcmp(mode, "all") == 0)
        printf("   output_nvenc.mp4          — NVENC encode\n");
    if (strcmp(mode, "transcode") == 0 || strcmp(mode, "all") == 0) {
        printf("   output_gpu_transcode.mp4  — GPU transcode (decode→scale_cuda→NVENC)\n");
        printf("   output_wrong_way.mp4      — Wrong way (decode→GPU→CPU→libx264)\n");
    }
    printf("========================================================\n");
    return 0;
}
