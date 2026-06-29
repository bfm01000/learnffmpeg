# 12_Hardware_Codec_Demo — 三平台硬件编解码对比演示

三个平台、同一套任务（解码/编码/转码）、**同一套 FFmpeg API 骨架**（Android 除外），侧重点在看清 API 差异而非跑功能。

## 为什么要有这个项目

FFmpeg 的硬件加速文档以 CLI 为主。实际开发中 **API 调用才是日常**，且三个平台的 API 差异足够大——分开学容易只见树木不见森林。这个项目把 NVIDIA / Android / Apple 的硬件编解码实现**放在一起**，命名对齐、结构对齐，方便对照阅读。

> 配合文档: [07-硬件编解码.md](../Doc/ffmpeg/07-硬件编解码.md)（通用底座）、[13-NVIDIA硬件编解码.md](../Doc/ffmpeg/13-NVIDIA硬件编解码.md)、[14-Android硬件编解码.md](../Doc/ffmpeg/14-Android硬件编解码.md)、[15-iOS硬件编解码.md](../Doc/ffmpeg/15-iOS硬件编解码.md)

## 项目结构

```
12_Hardware_Codec_Demo/
  CMakeLists.txt                 # 平台检测 + 条件编译
  README.md                      # 本文件
  .gitignore
  demo/
    nvidia_hw_codec.cpp          # ★ Linux + CUDA: 可编译运行
    android_hw_codec.cpp         #   Android NDK:  参考实现(需交叉编译)
    apple_hw_codec.cpp           #   macOS:        可编译运行
  scripts/
    build_nvidia.sh              # 一键构建+运行 NVIDIA demo
```

## 三平台 API 差异速览

> 这张表是学习三平台差异的入口。后面每个模式的注释里都有三个 `★` 标记和另外两个平台的对比。

| 维度 | NVIDIA (CUDA/NVENC) | Android (MediaCodec) | Apple (VideoToolbox) |
|---|---|---|---|
| **解码 API** | FFmpeg `avcodec` + `AV_HWDEVICE_TYPE_CUDA` | `AMediaCodec` (NDK) 或 `MediaCodec` (Java) | FFmpeg `avcodec` + `AV_HWDEVICE_TYPE_VIDEOTOOLBOX` |
| **编码 API** | FFmpeg `h264_nvenc` | `AMediaCodec` (encoding mode) | FFmpeg `h264_videotoolbox` |
| **GPU 帧格式** | `AV_PIX_FMT_CUDA` (CUdeviceptr) | `Surface` (gralloc/dma-buf) 或 `AImage` (ByteBuffer) | `AV_PIX_FMT_VIDEOTOOLBOX` (CVPixelBuffer) |
| **创建 GPU 连接** | `av_hwdevice_ctx_create(AV_HWDEVICE_TYPE_CUDA)` | `AMediaCodec_createDecoderByType(mime)` | `av_hwdevice_ctx_create(AV_HWDEVICE_TYPE_VIDEOTOOLBOX)` |
| **帧池** | `av_hwframe_ctx_alloc` + `av_hwframe_ctx_init` (显式) | VT 内部自管理 (CVPixelBufferPool) 或显式 `AImageReader` | 只需挂 `hw_device_ctx` (VT 自管池) |
| **CPU 读帧** | `av_hwframe_transfer_data(hw→sw)` | `AImage_getPlaneRowStride` + 逐行 `memcpy` | `CVPixelBufferLockBaseAddress` |
| **零拷贝渲染** | CUDA↔OpenGL/D3D interop | `Surface` → `Surface` (BufferQueue) | `CVPixelBuffer` → `CVMetalTextureCache` → Metal |
| **比特流格式** | Annex-B (`00 00 00 01`) | Annex-B (`00 00 00 01`) | ★ **AVCC** (长度前缀) |
| **颜色格式碎片化** | ❌ 无 (CUDA 统一 NV12) | ★★★ 严重 (I420/NV12/tiled) | ❌ 无 (统一) |
| **动态改参数** | `av_opt_set` (已打开 codec) | `setParameters` (运行时,不需重开) | `VTSessionSetProperty` |
| **硬解退软解** | FFmpeg 自动 fallback | ★ 手动检测 + 切换 codec | FFmpeg 自动 fallback |

## 构建和运行

### NVIDIA (Linux + CUDA GPU)

```bash
# 一键构建+运行 (自动生成测试视频)
./scripts/build_nvidia.sh run

# 或手动
cmake -S . -B build && cmake --build build -j
./build/nvidia_hw_codec <input.mp4>

# 只跑某个模式
./build/nvidia_hw_codec test.mp4 decode     # 只硬件解码
./build/nvidia_hw_codec test.mp4 transcode  # GPU 转码+错误对比
```

前置条件:
- NVIDIA GPU + 驱动
- FFmpeg 编译时带 `--enable-cuda --enable-cuvid --enable-nvenc --enable-libnpp`
- 验证: `ffmpeg -hwaccels | grep cuda`

### Apple (macOS + M 系列芯片)

```bash
# 只能在 macOS 上编译运行
cmake -S . -B build && cmake --build build -j
./build/apple_hw_codec <input.mp4>
```

前置条件:
- macOS 10.13+ (VideoToolbox 系统自带)
- FFmpeg 编译时带 `--enable-videotoolbox`
- 验证: `ffmpeg -codecs | grep videotoolbox`

### Android (NDK 交叉编译)

```bash
# 需要 Android NDK + CMake toolchain
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24
cmake --build build -j
```

> `android_hw_codec.cpp` 是参考实现。在桌面 Linux 上**不会编译**——代码仅供对照学习。

## 代码导航 (按学习路径)

**如果这是你第一次接触硬件编解码:**

1. 先读 `demo/nvidia_hw_codec.cpp` 的 MODE 1 (硬件解码) 注释
2. 然后看 `demo/apple_hw_codec.cpp` 的 MODE 1 ——理解 "同一套 FFmpeg API, 换个 `AV_HWDEVICE_TYPE_*` 就是另一个平台"
3. 再看 `demo/android_hw_codec.cpp` 的 MODE 1 ——理解"Android 为什么不同、为什么不走 FFmpeg 编解码"

**如果关注零拷贝和性能:**

1. `nvidia_hw_codec.cpp` MODE 3 (全 GPU 转码) vs MODE 4 (GPU→CPU→CPU 错误示范)
2. `android_hw_codec.cpp` MODE 3 (Surface 编码 —— 帧根本不下 CPU)
3. `apple_hw_codec.cpp` MODE 3 (CVPixelBuffer → Metal 互操作)

**如果关注各平台的坑:**

| 关注点 | 文件 | 模式 |
|---|---|---|
| 颜色格式/花屏 | `android_hw_codec.cpp` | MODE 2 (ByteBuffer→AImage) |
| AVCC vs Annex-B | `apple_hw_codec.cpp` | MODE 4 |
| PCIe 回读代价 | `nvidia_hw_codec.cpp` | MODE 4 (wrong way) |

## 关键学习路径对照

```
NVIDIA                         Apple                        Android
────────────────────────────────────────────────────────────────────
av_hwdevice_ctx_create     av_hwdevice_ctx_create     AMediaCodec_createDecoderByType
  (CUDA)                      (VIDEOTOOLBOX)              (video/avc)

av_hwframe_ctx_alloc       (VT自管池,无需显式)        configure(format, surface)
av_hwframe_ctx_init                                    → COLOR_FormatSurface

avcodec_send_packet        avcodec_send_packet         queueInputBuffer
avcodec_receive_frame      avcodec_receive_frame       dequeueOutputBuffer
  → AV_PIX_FMT_CUDA          → AV_PIX_FMT_VT              → Surface (GPU)
  → data[0]=CUdeviceptr      → data[3]=CVPixelBuffer      → 或 AImage (CPU)

av_hwframe_transfer_data   av_hwframe_transfer_data    AImage_getPlaneData
  (GPU→CPU copy)             (GPU→CPU copy)               getPlaneRowStride
                                                       逐行 memcpy (手动!)

HW filter (scale_cuda)     HW filter (scale_vt)        (Android 无等效——
av_buffersink_get_frame    av_buffersink_get_frame      用 Surface→Surface)
```

## 常见问题

**Q: 为什么 Android 不统一用 FFmpeg 的 `h264_mediacodec`？**

FFmpeg 6.0 起确实加了 `h264_mediacodec` 编码器，但它出现晚、API level 支持有限、参数可控性远不如直调 `AMediaCodec`。生产环境基本还是 FFmpeg 做封装/音频解编码、视频硬编解直调系统 API 的混合架构。

**Q: NVIDIA 的 `hw_frames_ctx` 和 `hw_device_ctx` 有什么区别？**

`hw_device_ctx` = GPU 连接（一个进程一个即可）。`hw_frames_ctx` = 显存帧池（控制池大小、`sw_format`）。只设 `hw_device_ctx` 也能硬解（解码器内部建池），但设 `hw_frames_ctx` 可以精确控制 `sw_format`（NV12 vs P016）和池大小。

**Q: Apple 的 VT 为什么不需要显式建 `hw_frames_ctx`？**

VT 对 CVPixelBuffer 的管理封装得比 CUDA 更深——`VTDecompressionSession` 内部自动管理 `CVPixelBufferPool`，包括回收和重分配。FFmpeg 封装层把它透传了出来，所以你只需挂 `hw_device_ctx`，解码器自己搞定池。

## 下一步

- 把当前平台的 demo 跑通 → 再读另外两个平台的代码（不通也能学 API 差异）
- 改参数（分辨率/码率/编码器选项）观察行为和性能变化
- 在你自己的项目里参考这里的 `av_hwdevice_ctx_create` / `av_hwframe_transfer_data` 使用姿势
- 读对应文档（见页首链接）深入原理层
