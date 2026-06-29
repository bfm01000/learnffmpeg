// ============================================================================
// android_hw_codec.cpp — Android MediaCodec 硬件编解码参考实现
//
// ★ 编译条件: Android NDK (CMake toolchain + libmediandk)
//   本文件在桌面 Linux 上无法编译——需交叉编译到 Android 目标。
//   它是学习资料：和 nvidia_hw_codec.cpp / apple_hw_codec.cpp 对照阅读，
//   理解三平台硬件编解码 API 的本质差异。
//
// 架构定位:
//   Android 的硬件编解码不走 FFmpeg 的编解码 API（h264_mediacodec 编码器
//   不成熟，生产不用）。主流做法是: FFmpeg 做封装/解封装/音频，视频编解码
//   直接调 Android NDK 的 AMediaCodec。
//
// 和三平台的关键差异一览:
//   ┌──────────┬─────────────────────┬───────────────────┬──────────────────┐
//   │          │ NVIDIA (CUDA)       │ Android            │ Apple (VideoTB)  │
//   ├──────────┼─────────────────────┼───────────────────┼──────────────────┤
//   │ 解码 API │ FFmpeg avcodec      │ AMediaCodec(NDK)  │ FFmpeg avcodec   │
//   │          │ + hw_device_ctx     │ 或 MediaCodec(Java)│ + hw_device_ctx   │
//   ├──────────┼─────────────────────┼───────────────────┼──────────────────┤
//   │ GPU 帧   │ AV_PIX_FMT_CUDA     │ Surface(gralloc)  │ AV_PIX_FMT_      │
//   │ 载体     │ CUdeviceptr         │ 或 AImage(ByteBuf)│ VIDEOTOOLBOX     │
//   ├──────────┼─────────────────────┼───────────────────┼──────────────────┤
//   │ CPU 读帧 │ av_hwframe_         │ AImageReader +    │ CVPixelBuffer     │
//   │          │ transfer_data       │ AImage            │ LockBaseAddress   │
//   ├──────────┼─────────────────────┼───────────────────┼──────────────────┤
//   │ 零拷贝   │ CUDA-GL interop     │ Surface → Surface │ CVPixelBuffer     │
//   │ 渲染     │                     │ (BufferQueue)     │ → Metal           │
//   ├──────────┼─────────────────────┼───────────────────┼──────────────────┤
//   │ 比特流   │ Annex-B (相同)       │ Annex-B           │ AVCC (与另两者   │
//   │ 格式     │                     │                   │  相反!)           │
//   ├──────────┼─────────────────────┼───────────────────┼──────────────────┤
//   │ SPS/PPS  │ extradata            │ csd-0/csd-1       │ format desc      │
//   │ 传递     │                     │ (MediaFormat)     │                  │
//   ├──────────┼─────────────────────┼───────────────────┼──────────────────┤
//   │ 硬解回退 │ 自动(FFmpeg 处理)    │ 手动检测 +         │ 自动(FFmpeg      │
//   │ 软解     │                     │ 切软件 codec      │ 处理)            │
//   ├──────────┼─────────────────────┼───────────────────┼──────────────────┤
//   │ 颜色格式 │ 无坑(CUDA 统一)      │ ★ 碎片化严重      │ 无坑(Apple 统一)  │
//   │          │                     │ I420/NV12/tiled   │                  │
//   └──────────┴─────────────────────┴───────────────────┴──────────────────┘
//
// 参考: Doc/ffmpeg/14-Android硬件编解码.md, Doc/ffmpeg/10-移动端硬件编解码.md
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>

// ── Android NDK 头文件 (仅在 Android 目标上存在) ──
#include <media/NdkMediaCodec.h>     // AMediaCodec — NDK 编解码 API
#include <media/NdkMediaFormat.h>    // AMediaFormat — 参数配置
#include <media/NdkMediaError.h>     // AMEDIA_OK 等返回码
#include <media/NdkImageReader.h>    // AImageReader — CPU 读帧 (ByteBuffer 替代)
#include <media/NdkImage.h>          // AImage — 按 plane/rowStride 取像素

// ── ANativeWindow (Surface 的 native 端) ──
#include <android/native_window.h>   // ANativeWindow — Surface 生产/消费
#include <android/native_window_jni.h>

// ============================================================
// 工具函数
// ============================================================

static const char* amedia_errstr(media_status_t status) {
    switch (status) {
        case AMEDIA_OK:                    return "OK";
        case AMEDIA_ERROR_UNKNOWN:         return "UNKNOWN";
        case AMEDIA_ERROR_MALFORMED:       return "MALFORMED";
        case AMEDIA_ERROR_UNSUPPORTED:     return "UNSUPPORTED";
        case AMEDIA_ERROR_INVALID_OBJECT:  return "INVALID_OBJECT";
        case AMEDIA_ERROR_INVALID_PARAMETER: return "INVALID_PARAMETER";
        case AMEDIA_ERROR_IO:              return "IO";
        case AMEDIA_DRM_NOT_PROVISIONED:   return "DRM_NOT_PROVISIONED";
        case AMEDIA_DRM_RESOURCE_BUSY:     return "DRM_RESOURCE_BUSY";
        default: return "UNKNOWN_STATUS";
    }
}

// ============================================================
// MODE 1: 硬件解码 (Surface 输出 → 零拷贝渲染)
//
// 路径: MediaExtractor 拆封装 → AMediaCodec 解码
//       → releaseOutputBuffer(idx, true) 渲染到 Surface
//
// ★ 跟 NVIDIA AV_HWDEVICE_TYPE_CUDA 的本质区别:
//   NVIDIA: 你调 avcodec_receive_frame → 拿到 AVFrame (GPU 指针)
//           你要自己决定: 放 GPU (零拷贝) 还是 transfer 回 CPU
//   Android: 你 configure 时传 Surface → 解码结果直接渲染
//           你根本不需要碰帧数据——连 AVFrame 都不存在
//   "Android 的 Surface 模式 = NVIDIA 的 hw_frames_ctx + 全程 GPU"
// ============================================================
static void mode_decode_surface(/* ANativeWindow* surface, */ const char* mime) {
    printf("\n========== MODE 1: AMediaCodec Decode (Surface output) ==========\n");

    // ── 1. 创建解码器 ──
    //    和 NVIDIA 的 avcodec_find_decoder + AV_HWDEVICE_TYPE_CUDA 对比:
    //    Android 根据 MIME 类型自动选择硬解还是软解——
    //    "video/avc" → 高通: OMX.qcom.video.decoder.avc (硬)
    //                → Google: c2.android.avc.decoder (软)
    //    不需要你在代码里区分，AMediaCodec 帮你选了
    AMediaCodec* codec = AMediaCodec_createDecoderByType(mime);
    if (!codec) { fprintf(stderr, "ERROR: createDecoderByType(%s)\n", mime); return; }

    // ── 2. 配置解码器 ──
    //    ★ configure 三个参数的含义:
    //       format: 输入参数 (MIME, 宽高, csd)
    //       surface: 输出目标——这是 Surface 模式的关键
    //       crypto: DRM (不加密传 nullptr)
    //       flags: 0=解码, CONFIGURE_FLAG_ENCODE=编码
    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mime);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, 1920);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, 1080);

    // ★ 颜色格式: COLOR_FormatSurface → 零拷贝
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT,
                          AMEDIACODEC_COLOR_FormatSurface);  // 21 = Surface 模式

    // 对比: 如果用 ByteBuffer 模式，设 COLOR_FormatYUV420Flexible
    // AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT,
    //                       AMEDIACODEC_COLOR_FormatYUV420Flexible);

    // CSD = Codec-Specific Data, 和 FFmpeg 的 extradata 对应
    // H.264: csd-0 = SPS, csd-1 = PPS (带起始码 00 00 00 01)
    // HEVC:  csd-0 = VPS+SPS+PPS 拼一起
    // 从 MediaExtractor 的 track format 直接拿就行
    // AMediaFormat_setBuffer(format, "csd-0", sps_data, sps_size);
    // AMediaFormat_setBuffer(format, "csd-1", pps_data, pps_size);

    media_status_t status = AMediaCodec_configure(codec, format,
                                                   nullptr,  // ★ Surface 传这里
                                                   nullptr,  // crypto
                                                   0);       // flags: 解码
    if (status != AMEDIA_OK) {
        fprintf(stderr, "ERROR: configure: %s\n", amedia_errstr(status));
        return;
    }
    AMediaFormat_delete(format);

    // ── 3. 启动 → 喂数据 → 取输出 ──
    AMediaCodec_start(codec);

    // 解码循环 (简化示意——实际要用 MediaExtractor 读输入):
    // while (has_input) {
    //     ssize_t idx = AMediaCodec_dequeueInputBuffer(codec, TIMEOUT_US);
    //     if (idx >= 0) {
    //         size_t buf_size;
    //         uint8_t* buf = AMediaCodec_getInputBuffer(codec, idx, &buf_size);
    //         // 把 Annex-B 数据拷进 buf
    //         AMediaCodec_queueInputBuffer(codec, idx, 0, data_size, pts, 0);
    //     }
    //
    //     AMediaCodecBufferInfo info;
    //     ssize_t out_idx = AMediaCodec_dequeueOutputBuffer(codec, &info, TIMEOUT_US);
    //     if (out_idx >= 0) {
    //         // ★ Surface 模式: releaseOutputBuffer(idx, true)
    //         //   第二个参数 true = render to Surface
    //         //   数据从解码器直接进 GPU/Display 管线，没下 CPU
    //         AMediaCodec_releaseOutputBuffer(codec, out_idx, true);
    //     } else if (out_idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
    //         // 输出格式确定了——从这里读真实宽高/颜色格式
    //         AMediaFormat* out_fmt = AMediaCodec_getOutputFormat(codec);
    //         // 读 KEY_WIDTH, KEY_HEIGHT, KEY_STRIDE, KEY_COLOR_FORMAT
    //         AMediaFormat_delete(out_fmt);
    //     }
    // }

    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);

    printf("[MODE 1] Surface decode done — 0 CPU pixels touched.\n");
    printf("  Compare: NVIDIA uses avcodec_receive_frame → AV_PIX_FMT_CUDA\n");
    printf("           Apple uses avcodec_receive_frame → AV_PIX_FMT_VIDEOTOOLBOX\n");
    printf("           Android uses Surface — you never see the frame data.\n");
}

// ============================================================
// MODE 2: 硬件解码 (ByteBuffer 模式 → AImage 读像素)
//
// 路径: 和 MODE 1 一样 decode，但输出走 ByteBuffer
//       → getOutputImage / AImage → 按 rowStride/pixelStride 读
//
// ★ 这是 Android 独有的坑——NVIDIA/Apple 没有颜色格式碎片化问题
//   为什么还要用 ByteBuffer？——截图、AI 推理等确需 CPU 像素的场景
// ============================================================
static void mode_decode_bytebuffer_to_cpu() {
    printf("\n========== MODE 2: AMediaCodec Decode (ByteBuffer → AImage → CPU) ==========\n");

    // configure 时颜色格式改成 Flexible (不写死 NV12/I420)
    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, 1920);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, 1080);

    // ★ 关键: 用 Flexible 而不是写死 NV12
    //   这样解码器会选一个 Image API 能处理的格式输出
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT,
                          AMEDIACODEC_COLOR_FormatYUV420Flexible);

    AMediaCodec* codec = AMediaCodec_createDecoderByType("video/avc");
    AMediaCodec_configure(codec, format, nullptr, nullptr, 0);
    AMediaFormat_delete(format);
    AMediaCodec_start(codec);

    // ── 解码循环 (示意) ──
    // while (has_input) {
    //     // ... dequeueInputBuffer / queueInputBuffer ...
    //
    //     AMediaCodecBufferInfo info;
    //     ssize_t out_idx = AMediaCodec_dequeueOutputBuffer(codec, &info, TIMEOUT_US);
    //
    //     if (out_idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
    //         AMediaFormat* out_fmt = AMediaCodec_getOutputFormat(codec);
    //         int32_t width, height, stride, slice_height, color_fmt;
    //         AMediaFormat_getInt32(out_fmt, AMEDIAFORMAT_KEY_WIDTH, &width);
    //         AMediaFormat_getInt32(out_fmt, AMEDIAFORMAT_KEY_HEIGHT, &height);
    //         AMediaFormat_getInt32(out_fmt, AMEDIAFORMAT_KEY_STRIDE, &stride);
    //         AMediaFormat_getInt32(out_fmt, AMEDIAFORMAT_KEY_SLICE_HEIGHT, &slice_height);
    //         AMediaFormat_getInt32(out_fmt, AMEDIAFORMAT_KEY_COLOR_FORMAT, &color_fmt);
    //         // ★ 以这里的 stride/slice_height 为准！
    //         AMediaFormat_delete(out_fmt);
    //         continue;
    //     }
    //
    //     if (out_idx >= 0) {
    //         // ── ★ 核心: 用 AImage 从 output buffer 读像素 ──
    //         //    和 NVIDIA 的 av_hwframe_transfer_data(hw→sw) 对比:
    //         //    NVIDIA: 一次函数调用, FFmpeg 内部处理 stride/格式转换
    //         //    Android: 你要手动按 plane/rowStride/pixelStride 逐行读
    //
    //         AImage* image = nullptr;
    //         media_status_t st = AMediaCodec_getOutputImage(codec, out_idx, &image);
    //
    //         if (st == AMEDIA_OK && image != nullptr) {
    //             int32_t num_planes;
    //             AImage_getNumberOfPlanes(image, &num_planes);
    //
    //             for (int p = 0; p < num_planes; p++) {
    //                 uint8_t* data;
    //                 int data_len;
    //                 int32_t row_stride, pixel_stride;
    //
    //                 AImage_getPlaneData(image, p, &data, &data_len);
    //                 AImage_getPlaneRowStride(image, p, &row_stride);
    //                 AImage_getPlanePixelStride(image, p, &pixel_stride);
    //
    //                 // ★ row_stride ≥ width (有对齐填充)
    //                 //   pixel_stride: Y=1, NV12_UV=2, I420_U/V=1
    //                 //
    //                 // 正确读法: 逐行拷贝，每行只取有效部分
    //                 // for (int row = 0; row < plane_height; row++) {
    //                 //     memcpy(dst + row * width,
    //                 //            data + row * row_stride,
    //                 //            width * pixel_stride);
    //                 // }
    //
    //                 printf("  Plane %d: rowStride=%d pixelStride=%d len=%d\n",
    //                        p, row_stride, pixel_stride, data_len);
    //             }
    //             AImage_delete(image);
    //         } else {
    //             // 旧设备 fallback: ByteBuffer
    //             // 坑: stride/slice_height/tiled 格式全要自己处理
    //             // 这就是 §7.4 步骤 3-4 讲的问题
    //         }
    //
    //         // ByteBuffer 模式: releaseOutputBuffer(idx, false)
    //         //   false = 不渲染 (因为数据你已经拷走了)
    //         AMediaCodec_releaseOutputBuffer(codec, out_idx, false);
    //     }
    // }

    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);

    printf("[MODE 2] ByteBuffer→AImage decode done.\n");
    printf("  Key: AImagegetPlaneRowStride ≠ width — skip padding every row.\n");
    printf("       pixelStride=2 for NV12 UV interleaved.\n");
    printf("       Tiled format → unfixable in ByteBuffer mode → switch to Surface.\n");
}

// ============================================================
// MODE 3: 硬件编码 (Surface 输入 → 零拷贝采集)
//
// 路径: 相机/GL 渲染 → Surface (GPU纹理)
//       → AMediaCodec.createInputSurface() → 编码器直接取 GPU 帧
//       → 输出 H.264 Annex-B 比特流
//
// ★ 跟 NVIDIA NVENC 对比:
//   NVIDIA: 你调 avcodec_send_frame(hw_frame) → hw_frame 是 GPU 帧
//           → avcodec_receive_packet → 编码后的 packet
//   Android: 你调 createInputSurface() 拿一个 Surface
//           → 相机/GL 往这个 Surface 上画
//           → AMediaCodec 自动从 Surface 取帧编码
//           同样是不需要碰像素数据
// ============================================================
static void mode_encode_surface() {
    printf("\n========== MODE 3: AMediaCodec Encode (Surface input) ==========\n");

    AMediaCodec* codec = AMediaCodec_createEncoderByType("video/avc");

    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, 1920);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, 1080);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, 4000000);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, 30);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);
    // ★ 颜色格式: Surface → 编码器输入来自 GPU
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT,
                          AMEDIACODEC_COLOR_FormatSurface);

    AMediaCodec_configure(codec, format, nullptr, nullptr,
                          AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    AMediaFormat_delete(format);

    // ── ★ 核心: createInputSurface ──
    //    返回一个 ANativeWindow*, 这个 Surface 的另一端连着编码器输入
    //    往这个 Surface 渲染的任何内容都会直接进编码器——零拷贝
    ANativeWindow* input_surface = AMediaCodec_createInputSurface(codec);

    // 典型用法: 把这个 Surface 设给 camera2 的 preview surface,
    // 或设给 EGL 的 render target
    //  相机预览  → input_surface → 编码器 → H.264 比特流
    //  GL 离屏渲染 → input_surface → 编码器 → H.264 比特流
    // 两条路径都是 GPU→encoder 零拷贝

    AMediaCodec_start(codec);

    // ── 编码循环 (示意) ──
    // while (encoding) {
    //     AMediaCodecBufferInfo info;
    //     ssize_t out_idx = AMediaCodec_dequeueOutputBuffer(codec, &info, TIMEOUT_US);
    //     if (out_idx >= 0) {
    //         size_t buf_size;
    //         uint8_t* buf = AMediaCodec_getOutputBuffer(codec, out_idx, &buf_size);
    //
    //         if (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) {
    //             // ★ 首帧: CSD (SPS+PPS), 单独存
    //             // 和 FFmpeg 的 extradata 一样的概念, 但通过 buffer flag 返回
    //         } else {
    //             // 普通帧: Annex-B H.264 NALU
    //             // 直接打进 MP4/RTP——和 NVIDIA h264_nvenc 输出一样
    //         }
    //         AMediaCodec_releaseOutputBuffer(codec, out_idx, false);
    //     }
    // }

    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    ANativeWindow_release(input_surface);

    printf("[MODE 3] Surface encode done — 0 CPU pixels touched.\n");
    printf("  Compare: NVIDIA uses avcodec_send_frame(AV_PIX_FMT_CUDA frame)\n");
    printf("           Apple uses avcodec_send_frame(AV_PIX_FMT_VIDEOTOOLBOX frame)\n");
    printf("           Android: you push frames via Surface (GPU rendering)\n");
    printf("  Commonality: all 3 platforms avoid GPU→CPU readback in hot path.\n");
}

// ============================================================
// MODE 4: 动态改变编码参数 (码率 / 强制关键帧)
//
// ★ 这是 Android 相比 NVIDIA/Apple 独特的 API:
//   setParameters 可以在编码器运行时动态改参数，
//   不需要像 FFmpeg 那样 reopen codec
// ============================================================
static void mode_dynamic_params() {
    printf("\n========== MODE 4: Dynamic Encode Parameters ==========\n");

    // 编码器已经跑起来之后:
    // AMediaCodec* codec = ...;  // running encoder

    // ── 动态降码率 (WebRTC GCC 拥塞控制) ──
    // AMediaFormat* params = AMediaFormat_new();
    // AMediaFormat_setInt32(params, AMEDIAFORMAT_KEY_VIDEO_BITRATE, 1500000);
    // AMediaCodec_setParameters(codec, params);
    // AMediaFormat_delete(params);

    // ── 强制产 IDR (对端 PLI/FIR → "我花屏了, 给个关键帧") ──
    // AMediaFormat* params = AMediaFormat_new();
    // AMediaFormat_setInt32(params, AMEDIAFORMAT_KEY_REQUEST_SYNC_FRAME, 0);
    // AMediaCodec_setParameters(codec, params);
    // AMediaFormat_delete(params);

    printf("[MODE 4] Dynamic params: setParameters(bitrate) + REQUEST_SYNC_FRAME\n");
    printf("  NVENC: similar via av_opt_set on opened encoder\n");
    printf("  VT: similar via VTSessionSetProperty(kVTCompressionPropertyKey_*)\n");
}

// ============================================================
// main() — 演示入口 (Android 上运行)
// ============================================================
int main() {
    printf("========================================================\n");
    printf(" Android AMediaCodec Hardware Codec Demo (Reference)\n");
    printf("========================================================\n");
    printf("\n");
    printf("This file is a reference implementation. It compiles\n");
    printf("only with the Android NDK CMake toolchain.\n");
    printf("\n");
    printf("Key differences from NVIDIA (nvidia_hw_codec.cpp):\n");
    printf("  1. No av_hwdevice_ctx_create — use AMediaCodec directly\n");
    printf("  2. No av_hwframe_transfer_data — use AImage for CPU read\n");
    printf("  3. Surface mode is the DEFAULT, not an opt-in\n");
    printf("  4. Color format FRAGMENTATION is the #1 Android pain point\n");
    printf("  5. Annex-B bitstream (same as NVIDIA, opposite of Apple)\n");
    printf("\n");

    mode_decode_surface("video/avc");
    mode_decode_bytebuffer_to_cpu();
    mode_encode_surface();
    mode_dynamic_params();

    printf("\n========================================================\n");
    printf(" Done. See Doc/ffmpeg/14-Android硬件编解码.md for details.\n");
    printf("========================================================\n");
    return 0;
}
