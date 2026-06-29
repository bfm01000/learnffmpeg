// ============================================================================
// apple_hw_codec.cpp — Apple VideoToolbox 硬件编解码参考实现
//
// ★ 编译条件: macOS + Xcode (VideoToolbox framework)
//   本文件在 Linux 上无法编译——需在 macOS 上运行。
//   和 nvidia_hw_codec.cpp / android_hw_codec.cpp 对照阅读，
//   理解三平台硬件编解码 API 的本质差异。
//
// FFmpeg 的角色:
//   和 NVIDIA CUDA 一样，Apple 的硬件编解码通过 FFmpeg 的 avcodec API 完成。
//   底层 FFmpeg 调用 VideoToolbox 的 VTDecompressionSession / VTCompressionSession。
//   这和 Android 不同——Android 生产环境通常绕过 FFmpeg 直调 AMediaCodec。
//
// 和三平台的关键差异:
//   ┌──────────────┬─────────────────┬──────────────────┬──────────────────┐
//   │              │ NVIDIA (CUDA)   │ Android (MC)     │ Apple (VT)       │
//   ├──────────────┼─────────────────┼──────────────────┼──────────────────┤
//   │ FFmpeg role  │ ✅ 编解码主力   │ ❌ 软解/封装/音频 │ ✅ 编解码主力    │
//   ├──────────────┼─────────────────┼──────────────────┼──────────────────┤
//   │ 编码器       │ h264_nvenc      │ AMediaCodec      │ h264_videotoolbox│
//   │              │ hevc_nvenc      │ (AMediaCodec)    │ hevc_videotoolbox│
//   ├──────────────┼─────────────────┼──────────────────┼──────────────────┤
//   │ 解码器       │ h264_cuvid 或   │ AMediaCodec      │ h264_videotoolbox│
//   │              │ hwaccel cuda    │                  │ (或 hwaccel VT)  │
//   ├──────────────┼─────────────────┼──────────────────┼──────────────────┤
//   │ HW 设备类型  │ CUDA            │ (无，不走FFmpeg) │ VIDEOTOOLBOX     │
//   ├──────────────┼─────────────────┼──────────────────┼──────────────────┤
//   │ 比特流       │ Annex-B         │ Annex-B          │ ★ AVCC           │
//   │              │ (00 00 00 01)   │ (00 00 00 01)    │ (长度前缀)        │
//   ├──────────────┼─────────────────┼──────────────────┼──────────────────┤
//   │ SPS/PPS      │ extradata       │ csd-0/csd-1      │ extradata (AVCC) │
//   ├──────────────┼─────────────────┼──────────────────┼──────────────────┤
//   │ 零拷贝渲染   │ CUDA-GL interop │ BufferQueue      │ CVPixelBuffer    │
//   │              │                 │ (Surface)        │ → Metal          │
//   ├──────────────┼─────────────────┼──────────────────┼──────────────────┤
//   │ CPU 读像素   │ av_hwframe_     │ AImageReader +   │ CVPixelBuffer    │
//   │              │ transfer_data   │ AImage           │ LockBaseAddress  │
//   ├──────────────┼─────────────────┼──────────────────┼──────────────────┤
//   │ 硬编画质     │ 中上(各代差异)   │ 因厂商而异       │ ★ M 系 Media     │
//   │              │                  │                  │ Engine 极高      │
//   ├──────────────┼─────────────────┼──────────────────┼──────────────────┤
//   │ 动态改参数   │ av_opt_set       │ setParameters    │ VTSessionSet     │
//   │              │ (重开 codec 也   │ (运行时,不需     │ Property (类似    │
//   │              │  可以)           │  重开)           │ Android)         │
//   └──────────────┴─────────────────┴──────────────────┴──────────────────┘
//
// 参考: Doc/ffmpeg/07-硬件编解码.md, Doc/ffmpeg/15-iOS硬件编解码.md
// ============================================================================

extern "C" {
// ── FFmpeg 核心头文件 ──
#include <libavcodec/avcodec.h>       // 编解码器 API
#include <libavformat/avformat.h>     // 解封装/封装 API
#include <libavutil/avutil.h>         // AV_PIX_FMT_*, av_log
#include <libavutil/hwcontext.h>      // ★ av_hwdevice_ctx_create, AVHWFramesContext
#include <libavutil/hwcontext_videotoolbox.h> // ★ AV_HWDEVICE_TYPE_VIDEOTOOLBOX
#include <libavutil/opt.h>            // av_opt_set
#include <libavutil/pixdesc.h>        // av_get_pix_fmt_name
#include <libswscale/swscale.h>       // sws_scale (仅 CPU fallback 对比)
}

#include <cstdio>
#include <cstdlib>
#include <cstring>

// ── Apple 原生框架 (仅在 macOS/iOS 上存在) ──
// 这些 include 在 Linux 上编译时会报错——本文件只能在 macOS 编译
#include <VideoToolbox/VideoToolbox.h>   // VTDecompressionSession, VTCompressionSession
#include <CoreVideo/CoreVideo.h>         // CVPixelBuffer, CVPixelBufferLockBaseAddress
#include <CoreMedia/CoreMedia.h>         // CMTime, CMBlockBuffer

// ============================================================
// 工具函数
// ============================================================

static const char* errstr(int err) {
    static thread_local char buf[256];
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

static void print_frame_info(const char* tag, AVFrame* frame) {
    const char* fmt_name = av_get_pix_fmt_name((AVPixelFormat)frame->format);
    bool is_hw = (frame->format == AV_PIX_FMT_VIDEOTOOLBOX);
    printf("  [%s] fmt=%s(%s) %dx%d pts=%ld\n",
           tag,
           fmt_name ? fmt_name : "unknown",
           is_hw ? "GPU_VT" : "CPU",
           frame->width, frame->height, frame->pts);
}

// ============================================================
// 创建 VideoToolbox 硬件设备上下文
//
// ★ 和 NVIDIA create_cuda_device() 对比:
//   NVIDIA: av_hwdevice_ctx_create(AV_HWDEVICE_TYPE_CUDA, ...)
//           需要 CUDA driver + FFmpeg --enable-cuda
//   Apple:  av_hwdevice_ctx_create(AV_HWDEVICE_TYPE_VIDEOTOOLBOX, ...)
//           不需要额外驱动——macOS 自带 VideoToolbox
//
//   Apple 的 hw_device_ctx 和 NVIDIA 做的事一样:
//   建一个 session 跟 GPU/Media Engine 通信
// ============================================================
static AVBufferRef* create_vt_device() {
    AVBufferRef* hw_ctx = nullptr;
    int ret = av_hwdevice_ctx_create(&hw_ctx, AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
                                      nullptr, nullptr, 0);
    if (ret < 0) {
        fprintf(stderr, "ERROR: Failed to create VideoToolbox device: %s\n", errstr(ret));
        fprintf(stderr, "  VideoToolbox is macOS/iOS only.\n");
        exit(1);
    }
    printf("[VT] Hardware device context created (AV_HWDEVICE_TYPE_VIDEOTOOLBOX)\n");
    return hw_ctx;
}

// ============================================================
// MODE 1: 硬件解码 —— 帧留在 GPU
//
// 路径: 输入文件 → av_read_frame → avcodec_send_packet
//       → avcodec_receive_frame → AV_PIX_FMT_VIDEOTOOLBOX 帧 (CVPixelBuffer)
//       → 打印帧信息 (不下 CPU)
//
// ★ 和 NVIDIA 对比的关键差异:
//   1. AV_HWDEVICE_TYPE_VIDEOTOOLBOX vs AV_HWDEVICE_TYPE_CUDA
//   2. AV_PIX_FMT_VIDEOTOOLBOX   vs AV_PIX_FMT_CUDA
//   3. VT 解码不需要显式设 hw_frames_ctx——设 hw_device_ctx 就够
//      (VT 内部自己管理 CVPixelBufferPool)
//   4. Apple 硬解 = M 系列芯片的 Media Engine，功耗极低
// ============================================================
static void mode_decode(const char* input_file) {
    printf("\n========== MODE 1: VideoToolbox Hardware Decode ==========\n");

    // ── 1. 打开输入 ──
    AVFormatContext* fmt_ctx = nullptr;
    int ret = avformat_open_input(&fmt_ctx, input_file, nullptr, nullptr);
    if (ret < 0) { fprintf(stderr, "ERROR: avformat_open_input: %s\n", errstr(ret)); exit(1); }
    avformat_find_stream_info(fmt_ctx, nullptr);

    int video_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    AVCodecParameters* par = fmt_ctx->streams[video_idx]->codecpar;
    printf("[Input] %s, codec=%s, %dx%d\n",
           input_file, avcodec_get_name(par->codec_id), par->width, par->height);

    // ── 2. 打开解码器 ──
    const AVCodec* decoder = nullptr;

    // ★ Apple 有两种方式用硬件解码:
    //   方式 A: 用 h264_videotoolbox 解码器 (显式请求 VT)
    //           decoder = avcodec_find_decoder_by_name("h264_videotoolbox");
    //   方式 B: 用通用解码器 + hw_device_ctx (自动选 VT)
    //           decoder = avcodec_find_decoder(par->codec_id);
    //           挂 hw_device_ctx, FFmpeg 自动尝试硬解
    //
    //   方式 B 更灵活——硬解不可用时自动退软解。和 NVIDIA 的
    //   h264_cuvid(显式) vs hwaccel cuda(自动) 是一个道理。

    decoder = avcodec_find_decoder(par->codec_id);  // 方式 B: 自动
    if (!decoder) { fprintf(stderr, "ERROR: decoder not found\n"); exit(1); }

    AVCodecContext* codec_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(codec_ctx, par);

    // ── 3. ★ 核心: 挂 VideoToolbox 硬件设备上下文 ──
    //       注意: VT 解码通常只需要 hw_device_ctx，不需要 hw_frames_ctx
    //       VT 内部自动管理 CVPixelBufferPool——和 Android Surface 模式类似
    //       这是和 NVIDIA 的一个 API 差异:
    //       NVIDIA: hw_frames_ctx 必填 (控制池大小和 sw_format)
    //       VT:     只需 hw_device_ctx (VT 内部自管理)
    AVBufferRef* hw_device_ctx = create_vt_device();
    codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);  // ★ 只需这行

    ret = avcodec_open2(codec_ctx, decoder, nullptr);
    if (ret < 0) { fprintf(stderr, "ERROR: avcodec_open2: %s\n", errstr(ret)); exit(1); }

    // ── 4. 解码循环 ──
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    int frame_count = 0;

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index != video_idx) { av_packet_unref(pkt); continue; }

        avcodec_send_packet(codec_ctx, pkt);
        while (avcodec_receive_frame(codec_ctx, frame) == 0) {
            print_frame_info("DECODED", frame);
            // ★ frame->format == AV_PIX_FMT_VIDEOTOOLBOX
            //   frame->data[3] 是 CVPixelBufferRef (苹果的 GPU 帧容器)
            //   和 NVIDIA 的 frame->data[0]=CUdeviceptr 是同一个概念
            //   ——都是"不透明 GPU 句柄，CPU 不能直接读"
            //
            //   要和 Metal 互操作:
            //     CVPixelBufferRef cvpix = (CVPixelBufferRef)frame->data[3];
            //     CVMetalTextureCacheCreateTextureFromImage(..., cvpix, ...)
            //     → Metal 纹理，零拷贝进渲染管线
            frame_count++;
        }
        av_packet_unref(pkt);
    }
    // flush
    avcodec_send_packet(codec_ctx, nullptr);
    while (avcodec_receive_frame(codec_ctx, frame) == 0) {
        print_frame_info("DECODED(flush)", frame);
        frame_count++;
    }

    printf("\n[MODE 1] Decoded %d frames, all on GPU (CVPixelBuffer)\n", frame_count);

    av_frame_free(&frame);
    av_packet_free(&pkt);
    av_buffer_unref(&hw_device_ctx);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
}

// ============================================================
// MODE 2: 硬件编码 (h264_videotoolbox)
//
// 路径: 生成测试 YUV 帧 (CPU)
//       → av_hwframe_transfer_data (CPU→GPU 上传)
//       → avcodec_send_frame → h264_videotoolbox 编码
//       → avcodec_receive_packet → ★ AVCC 格式比特流
//
// ★ 和 NVIDIA NVENC 对比的关键差异:
//   1. 编码器名: h264_videotoolbox vs h264_nvenc
//   2. 输出比特流: ★ AVCC (长度前缀) vs Annex-B (00 00 00 01)
//      → iOS 推 RTP 必须做 AVCC→Annex-B 转换！这是经典坑
//      → Android 推 RTP 天然 Annex-B，不需要转换
//   3. M 系 Media Engine 能效比远超 NVENC——4K 编码几乎不发热
//   4. 参数控制不如 NVENC 丰富，但 API 更简洁
// ============================================================
static void mode_encode(const char* output_file, int width, int height, int frame_count) {
    printf("\n========== MODE 2: VideoToolbox Hardware Encode ==========\n");
    printf("[Encode] Output: %s, %dx%d, %d frames\n", output_file, width, height, frame_count);

    // ── 1. 打开 h264_videotoolbox 编码器 ──
    const AVCodec* encoder = avcodec_find_encoder_by_name("h264_videotoolbox");
    if (!encoder) {
        fprintf(stderr, "ERROR: h264_videotoolbox not found\n");
        fprintf(stderr, "  VideoToolbox is macOS/iOS only. Build FFmpeg with --enable-videotoolbox\n");
        exit(1);
    }

    AVCodecContext* enc_ctx = avcodec_alloc_context3(encoder);
    enc_ctx->width     = width;
    enc_ctx->height    = height;
    enc_ctx->time_base = (AVRational){1, 30};
    enc_ctx->framerate = (AVRational){30, 1};
    enc_ctx->pix_fmt   = AV_PIX_FMT_VIDEOTOOLBOX;  // ★ 输入是 GPU 帧
    enc_ctx->bit_rate  = 4000000;
    enc_ctx->gop_size  = 30;
    enc_ctx->max_b_frames = 0;

    // VT 特有参数 (和 NVENC 的 -preset p4 对应):
    av_opt_set(enc_ctx->priv_data, "realtime", "1", 0);       // 实时编码
    av_opt_set(enc_ctx->priv_data, "allow_sw", "0", 0);       // 只硬不软
    // NVENC 有 p1-p7 preset + tune + rc 三个旋钮
    // VT 只有 realtime + allow_sw + quality 几个——控制粒度不同

    // ── 2. 挂 VideoToolbox 硬件设备 ──
    AVBufferRef* hw_device_ctx = create_vt_device();
    AVBufferRef* hw_frames_ctx = nullptr;

    // VT 编码: 可以只设 hw_device_ctx
    // 如果需要控制 CVPixelBufferPool, 才显式建 hw_frames_ctx
    enc_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);

    int ret = avcodec_open2(enc_ctx, encoder, nullptr);
    if (ret < 0) { fprintf(stderr, "ERROR: avcodec_open2(enc): %s\n", errstr(ret)); exit(1); }

    // ── 3. 输出文件 ──
    AVFormatContext* out_fmt = nullptr;
    avformat_alloc_output_context2(&out_fmt, nullptr, nullptr, output_file);
    if (!out_fmt) avformat_alloc_output_context2(&out_fmt, nullptr, "mp4", output_file);
    AVStream* out_stream = avformat_new_stream(out_fmt, nullptr);
    avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
    out_stream->time_base = enc_ctx->time_base;
    avio_open(&out_fmt->pb, output_file, AVIO_FLAG_WRITE);
    avformat_write_header(out_fmt, nullptr);

    // ── 4. 编码循环 (CPU YUV → GPU upload → encode) ──
    AVFrame* sw_frame = av_frame_alloc();
    sw_frame->format = AV_PIX_FMT_NV12;  // VT 偏好 NV12
    sw_frame->width  = width;
    sw_frame->height = height;
    av_frame_get_buffer(sw_frame, 0);

    AVFrame* hw_frame = av_frame_alloc();
    AVPacket* enc_pkt = av_packet_alloc();

    for (int i = 0; i < frame_count; i++) {
        // 生成测试画面
        for (int y = 0; y < height; y++) {
            memset(sw_frame->data[0] + y * sw_frame->linesize[0],
                   (i * 5 + y * 255 / height) % 256, width);
        }
        sw_frame->pts = i;

        // ★ CPU→GPU 上传
        av_hwframe_get_buffer(enc_ctx->hw_frames_ctx, hw_frame, 0);
        av_hwframe_transfer_data(hw_frame, sw_frame, 0);

        avcodec_send_frame(enc_ctx, hw_frame);
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

    printf("\n[MODE 2] Encoded %d frames with VideoToolbox\n", frame_count);
    printf("  ★ Output bitstream format: AVCC (length-prefix NALU)\n");
    printf("     NOT Annex-B! iOS推RTP必须做 AVCC→Annex-B 转换\n");

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
// MODE 3: CVPixelBuffer → Metal 零拷贝渲染 (示意)
//
// ★ 和 NVIDIA CUDA-GL interop 对比:
//   NVIDIA: cudaGraphicsGLRegisterImage → 解码帧写 GL 纹理
//   Apple:  CVPixelBuffer → CVMetalTextureCache → Metal 纹理
//   两者都是"GPU 帧换个 API 句柄继续用"，不下 CPU
//
//   和 Android Surface 对比:
//   Android: 你甚至不需要做 interop——Surface 自动渲染
//   Apple:   你要显式调 CVMetalTextureCache (但也是一次 API 调用)
// ============================================================
static void mode_metal_interop_demo() {
    printf("\n========== MODE 3: CVPixelBuffer → Metal Interop (示意) ==========\n");

    // 假设解码得到了一个 hw_frame (AV_PIX_FMT_VIDEOTOOLBOX):
    // CVPixelBufferRef cvpix = (CVPixelBufferRef)hw_frame->data[3];
    //
    // // 创建 Metal 纹理缓存 (初始化时做一次):
    // CVMetalTextureCacheRef tex_cache;
    // CVMetalTextureCacheCreate(kCFAllocatorDefault, NULL,
    //                           metal_device, NULL, &tex_cache);
    //
    // // 每帧: CVPixelBuffer → Metal 纹理 (零拷贝!)
    // CVMetalTextureRef metal_tex;
    // CVMetalTextureCacheCreateTextureFromImage(
    //     kCFAllocatorDefault,
    //     tex_cache,
    //     cvpix,
    //     NULL,                          // 纹理属性
    //     MTLPixelFormatBGRA8Unorm,     // 或 R8Unorm (Y), RG8Unorm (UV)
    //     width, height, 0, &metal_tex);
    //
    // id<MTLTexture> texture = CVMetalTextureGetTexture(metal_tex);
    // // 现在 texture 可以被 Metal shader 直接采样——数据仍在 GPU
    // CFRelease(metal_tex);

    printf("[MODE 3] Metal interop: CVPixelBuffer → CVMetalTextureCache → MTLTexture\n");
    printf("  Same pattern as NVIDIA CUDA-GL interop:\n");
    printf("    NVIDIA: CUdeviceptr → cudaGraphicsGLRegisterImage → GL texture\n");
    printf("    Apple:  CVPixelBuffer → CVMetalTextureCache → MTLTexture\n");
    printf("    Android: Surface → BufferQueue → OES texture (automagic)\n");
}

// ============================================================
// MODE 4: 比特流格式转换 (AVCC → Annex-B)
//
// ★ 这是 Apple 独有的坑——iOS 推流必须做这个转换
//   NVIDIA NVENC 和 Android MediaCodec 都输出 Annex-B，不需要转
// ============================================================
static void mode_avcc_to_annexb_demo() {
    printf("\n========== MODE 4: AVCC → Annex-B Conversion (Apple-specific) ==========\n");

    // h264_videotoolbox 输出的 extradata 是 AVCC 格式:
    //   [4 bytes: SPS len][SPS data][1 byte: PPS count][2 bytes: PPS len][PPS data]...
    //
    // 推 RTP/FLV 需要转成 Annex-B:
    //   00 00 00 01 [SPS] 00 00 00 01 [PPS]
    //
    // 可以用 FFmpeg 的 av_bitstream_filter (旧) 或 bsf (新):
    //
    // // AVCC → Annex-B:
    // const AVBitStreamFilter* bsf = av_bsf_get_by_name("h264_mp4toannexb");
    // AVBSFContext* bsf_ctx;
    // av_bsf_alloc(bsf, &bsf_ctx);
    // avcodec_parameters_copy(bsf_ctx->par_in, enc_ctx->codecpar);
    // av_bsf_init(bsf_ctx);
    // // 用 av_bsf_send_packet / av_bsf_receive_packet 转换

    printf("[MODE 4] AVCC→Annex-B: use h264_mp4toannexb BSF\n");
    printf("  This conversion is NOT needed for:\n");
    printf("    - NVIDIA NVENC (outputs Annex-B natively)\n");
    printf("    - Android MediaCodec (outputs Annex-B natively)\n");
    printf("  It IS needed for:\n");
    printf("    - iOS/macOS VideoToolbox → RTP streaming\n");
    printf("    - VideoToolbox → FLV (RTMP) live push\n");
}

// ============================================================
// main() — 命令行入口 (macOS 上运行)
// ============================================================
int main(int argc, char** argv) {
    printf("========================================================\n");
    printf(" Apple VideoToolbox Hardware Codec Demo\n");
    printf("========================================================\n");
    printf("\n");
    printf("This file compiles and runs on macOS only.\n");
    printf("It uses VideoToolbox via FFmpeg API.\n");
    printf("\n");

    if (argc < 2) {
        printf("Usage: %s <input.mp4>\n", argv[0]);
        printf("\n");
        printf("Modes demonstrated:\n");
        printf("  1. Hardware decode (CVPixelBuffer stays on GPU)\n");
        printf("  2. Hardware encode (h264_videotoolbox, AVCC output)\n");
        printf("  3. Metal interop (CVPixelBuffer → MTLTexture, zero-copy)\n");
        printf("  4. AVCC→Annex-B conversion (Apple-specific pitfall)\n");
        printf("\n");
        printf("Compare with:\n");
        printf("  nvidia_hw_codec.cpp — CUDA/NVENC (Linux)\n");
        printf("  android_hw_codec.cpp — AMediaCodec (Android)\n");
        return 0;
    }

    const char* input = argv[1];

    mode_decode(input);
    mode_encode("output_vt.mp4", 1920, 1080, 90);
    mode_metal_interop_demo();
    mode_avcc_to_annexb_demo();

    printf("\n========================================================\n");
    printf(" Done.\n");
    printf(" See Doc/ffmpeg/15-iOS硬件编解码.md for details.\n");
    printf("========================================================\n");
    return 0;
}
