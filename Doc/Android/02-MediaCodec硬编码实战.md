# MediaCodec 硬编码实战：从 YUV 到 H.264 比特流

## 0. 本篇定位

- 面试复习：先能讲清 `ByteBuffer` 输入和 `Surface` 输入的差异，`INFO_OUTPUT_FORMAT_CHANGED`、SPS/PPS、关键帧和动态码率怎么处理。
- 深入学习：重点看线性内存、tiled layout、`AHardwareBuffer`/`GraphicBuffer` 与编码器之间的关系，理解为什么 Surface 输入更适合实时视频。
- 工程落点：实时推流优先选择 Camera/GL 写 Surface，再由 MediaCodec 编码，除非确实需要 CPU 算法逐帧处理。
> **适用方向**：Android 移动端音视频 SDK 开发，直播推流/RTC 方向
> **前置知识**：MediaCodec 基础（见 [[../ffmpeg/14-Android硬件编解码]]），YUV 像素格式
> **难度**：⭐⭐⭐（1-5 星）
> **预计阅读**：速记 8 分钟｜全文 25 分钟
> **关联文档**：[[00-Android音视频开发全景导读]] · [[01-Camera2采集详解]] · [[../ffmpeg/14-Android硬件编解码]]
> **定位**：🟢 中级必会 —— Android 实时推流的第二大核心

---

## 一、面试速记

| # | 考点 | 一句话答案 |
|---|------|-----------|
| 1 | MediaCodec 编码怎么创建 | createByCodecName("video/avc") → configure(encoder) → start → dequeue/queue → stop/release |
| 2 | 编码输入入口有几种 | 两种：ByteBuffer（CPU 填 YUV）和 Surface（GPU 直接渲染，零拷贝） |
| 3 | 编码吐什么格式 | **Annex-B**（00 00 00 01 起始码），SPS/PPS 由编码器自动产出在 csd-0/csd-1，configure 时不需要传 |
| 4 | 实时编码的关键配置 | KEY_BITRATE_MODE=CBR、KEY_LOW_LATENCY=1、I_FRAME_INTERVAL=1-2s、B 帧关掉(PRIORITY_REALTIME) |
| 5 | 怎么动态调码率 | setParameters(Bundle) 传 KEY_VIDEO_BITRATE |
| 6 | 怎么强制关键帧 | setParameters(Bundle) 传 KEY_REQUEST_SYNC_FRAME |
| 7 | 编码完怎么拿数据 | dequeueOutputBuffer → getOutputBuffer → 取 Annex-B 字节 → releaseOutputBuffer |

---

## 二、核心 Demo：MediaCodec H.264 编码器

```java
// MediaCodecEncoder.java
// 完整的 Android MediaCodec H.264 实时编码器
package com.example.encoder;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaFormat;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;

import java.nio.ByteBuffer;

public class MediaCodecEncoder {
    // ---- 配置 ----
    public enum OutputMode {
        SAFE_COPY,  // 默认：拷贝一份再传回调，release 在编码线程立即完成
        ZERO_COPY   // 零拷贝：原 buffer 直接传给回调，消费者必须调用 releaseFrame.run()
    }

    public interface EncoderCallback {
        // releaseFrame: 仅 ZERO_COPY 模式非 null，消费者用完后必须调用 releaseFrame.run()
        // SAFE_COPY 模式下 releaseFrame 为 null，数据已是独立拷贝
        void onEncodedFrame(ByteBuffer data, MediaCodec.BufferInfo info, boolean isKeyFrame,
                            Runnable releaseFrame);
    }

    // ---- 成员 ----
    private MediaCodec codec;
    private HandlerThread encoderThread;
    private Handler encoderHandler;
    private EncoderCallback callback;
    private MediaFormat format;
    private OutputMode outputMode;
    private int width, height, fps, bitrate;
    private boolean forceKeyFrame = false;

    // ============================================================
    // 初始化 & 创建编码器
    // ============================================================
    public MediaCodecEncoder(int width, int height, int fps, int bitrate,
                              EncoderCallback callback) {
        this(width, height, fps, bitrate, callback, OutputMode.SAFE_COPY);
    }

    public MediaCodecEncoder(int width, int height, int fps, int bitrate,
                              EncoderCallback callback, OutputMode outputMode) {
        this.width = width; this.height = height;
        this.fps = fps; this.bitrate = bitrate;
        this.callback = callback;
        this.outputMode = outputMode;

        encoderThread = new HandlerThread("EncoderThread");
        encoderThread.start();
        encoderHandler = new Handler(encoderThread.getLooper());
    }

    public void start() {
        try {
            // ① 找编码器: H.264 硬件优先
            String mime = MediaFormat.MIMETYPE_VIDEO_AVC;
            MediaCodecInfo codecInfo = selectCodec(mime);
            codec = MediaCodec.createByCodecName(codecInfo.getName());

            // ② 配置 MediaFormat
            format = MediaFormat.createVideoFormat(mime, width, height);
            format.setInteger(MediaFormat.KEY_COLOR_FORMAT,
                MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible);
                // ★ YUV420Flexible: 最兼容的格式，支持 NV12/NV21/I420
            format.setInteger(MediaFormat.KEY_BIT_RATE, bitrate);
            format.setInteger(MediaFormat.KEY_FRAME_RATE, fps);
            format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, fps * 2); // 2s GOP

            // ★ 实时编码关键配置
            format.setInteger(MediaFormat.KEY_BITRATE_MODE,
                MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CBR);  // 固定码率≈CBR
            format.setInteger(MediaFormat.KEY_COMPLEXITY,
                MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CBR);  // 简化示意
            // API 26+: 低延迟模式
            if (android.os.Build.VERSION.SDK_INT >= 26) {
                format.setInteger(MediaFormat.KEY_LATENCY, 1);  // 低延迟
            }
            // Profile
            format.setInteger(MediaFormat.KEY_PROFILE,
                MediaCodecInfo.CodecProfileLevel.AVCProfileBaseline);

            codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            codec.start();

            // ③ 启动编码循环
            encoderHandler.post(this::encodeLoop);
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    // ============================================================
    // 选择编码器（优先硬件）
    // ============================================================
    private MediaCodecInfo selectCodec(String mime) {
        MediaCodecList list = new MediaCodecList(MediaCodecList.ALL_CODECS);
        for (MediaCodecInfo info : list.getCodecInfos()) {
            if (!info.isEncoder()) continue;
            for (String type : info.getSupportedTypes()) {
                if (type.equalsIgnoreCase(mime)) {
                    // ★ 优先硬件编码器
                    if (info.getName().contains(" OMX.qcom.") ||
                        info.getName().contains(" OMX.mtk.")  ||
                        info.getName().contains(" c2.qti.")   ||
                        info.getName().contains(" c2.mtk.")) {
                        return info;
                    }
                }
            }
        }
        // 回退：第一个可用的
        return list.getCodecInfos()[0];
    }

    // ============================================================
    // 编码一帧 YUV (ByteBuffer 方式)
    // ============================================================
    public void encodeNV12(byte[] yuvData, long ptsUs) {
        if (codec == null) return;

        int inputIndex = codec.dequeueInputBuffer(10000); // 10ms timeout
        if (inputIndex < 0) return;

        ByteBuffer inputBuffer = codec.getInputBuffer(inputIndex);
        inputBuffer.clear();
        inputBuffer.put(yuvData);  // 填入 NV12 YUV 数据

        int flags = 0;
        if (forceKeyFrame) {
            // ★ 强制关键帧: 通过 Bundle 或 flag
            Bundle params = new Bundle();
            params.putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0);
            codec.setParameters(params);
            forceKeyFrame = false;
        }

        codec.queueInputBuffer(inputIndex, 0, yuvData.length, ptsUs, flags);
    }

    // ============================================================
    // 取编码输出
    // ============================================================
    private void encodeLoop() {
        MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();

        while (codec != null) {
            int outputIndex = codec.dequeueOutputBuffer(info, 10000);
            if (outputIndex >= 0) {
                ByteBuffer outputBuffer = codec.getOutputBuffer(outputIndex);

                if (info.size > 0 && callback != null) {
                    // ★ Android 编码输出已经是 Annex-B 格式
                    // 含 00 00 00 01 起始码，可直接推 RTP/RTMP
                    boolean isKeyFrame =
                        (info.flags & MediaCodec.BUFFER_FLAG_SYNC_FRAME) != 0;

                    if (outputMode == OutputMode.ZERO_COPY) {
                        // ★ 零拷贝路径: 直接传原 ByteBuffer 引用，消费者用完必须调 releaseFrame.run()
                        //   省掉 ~几十 KB 的拷贝，代价是 encodeLoop 线程被 hold 到网络发完
                        final int idx = outputIndex;
                        outputBuffer.position(info.offset);
                        outputBuffer.limit(info.offset + info.size);
                        callback.onEncodedFrame(outputBuffer, info, isKeyFrame,
                            () -> codec.releaseOutputBuffer(idx, false));
                        // 不在这里 release —— 消费者负责在合适的时机调用 releaseFrame
                    } else {
                        // ★ 安全拷贝路径（默认）: 拷贝后立即释放，编解码线程和网络线程完全解耦
                        byte[] frameData = new byte[info.size];
                        outputBuffer.position(info.offset);
                        outputBuffer.get(frameData, 0, info.size);

                        callback.onEncodedFrame(ByteBuffer.wrap(frameData), info, isKeyFrame,
                            null); // SAFE_COPY 不需要 releaseFrame
                        codec.releaseOutputBuffer(outputIndex, false);
                    }
                } else {
                    // 空帧（size == 0）：直接释放
                    codec.releaseOutputBuffer(outputIndex, false);
                }

            } else if (outputIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                // ★ 格式变化：可以用 MediaFormat 拿编码器产出的 SPS/PPS
                MediaFormat newFormat = codec.getOutputFormat();
                ByteBuffer sps = newFormat.getByteBuffer("csd-0"); // SPS
                ByteBuffer pps = newFormat.getByteBuffer("csd-1"); // PPS
                // Android 的 SPS/PPS 在 csd 里，但编码输出流里自带了 SPS/PPS NALU
                // 因为吐 Annex-B，IDR 前面已经有 SPS/PPS 起始码 NALU 了
            }
        }
    }

    // ============================================================
    // 动态控制
    // ============================================================
    public void setBitrate(int newBitrate) {
        if (codec != null) {
            Bundle params = new Bundle();
            params.putInt(MediaCodec.PARAMETER_KEY_VIDEO_BITRATE, newBitrate);
            codec.setParameters(params);
        }
    }

    public void forceKeyFrame() {
        forceKeyFrame = true;
    }

    // ============================================================
    // 停止
    // ============================================================
    public void stop() {
        if (codec != null) {
            codec.stop();
            codec.release();
            codec = null;
        }
        if (encoderThread != null) {
            encoderThread.quitSafely();
        }
    }
}
```

### 2.1 使用示例

```java
// ===== 默认：SAFE_COPY 模式（零改动，和原来一样）=====
MediaCodecEncoder encoder = new MediaCodecEncoder(1920, 1080, 30, 2_000_000,
    (data, info, isKeyFrame, releaseFrame) -> {
        // releaseFrame 为 null，data 是独立副本，可以慢慢发
        byte[] bytes = new byte[data.remaining()];
        data.get(bytes);
        sendToRtmp(bytes, info.presentationTimeUs);
    });

// ===== ZERO_COPY 模式 =====
MediaCodecEncoder encoder2 = new MediaCodecEncoder(1920, 1080, 30, 2_000_000,
    (data, info, isKeyFrame, releaseFrame) -> {
        // ★ data 是 MediaCodec 的原生 ByteBuffer，省了一次拷贝
        sendToRtmpDirect(data, info.presentationTimeUs, () -> {
            // ★ 网络发送完成后再释放 buffer，不阻塞编码器
            releaseFrame.run();
        });
    },
    MediaCodecEncoder.OutputMode.ZERO_COPY);  // ← 切到这里
```

---


### 2.2 底层原理：linear 与 tiled —— 为什么 Surface 输入是降维打击

> **一句话：输入侧有 tiled/linear 之争（Surface 赢），输出侧没有（码流是一维字节）。硬件编码器吃 Linear 数据一定会内部转成 Tiled——这个转换白浪费内存带宽、增加延迟、加剧发热。Surface 路径从源头就是 Tiled，全程零转换。**

#### 2.2.1 内存排布：linear 和 tiled 到底长什么样

**Linear（线性）排布**：像素像写文章一样，一行写完再写下一行。CPU 遍历友好——指针一路 `+1` 就能扫完整张图。

**Tiled（瓦片/块状）排布**：把图像切成无数个小方块（如 16×16 像素的宏块），每个方块内的像素在物理内存上**连续紧挨着存放**。

```
linear NV12（CPU 看到的）:              tiled（VPU/GPU 喜欢的，厂商私有格式）:
┌─────────────────────┐                 ┌────┬────┬────┬────┐
│ Y  row 0            │                 │MB00│MB01│MB02│MB03│
│ Y  row 1            │                 ├────┼────┼────┼────┤
│ ...                 │                 │MB10│MB11│MB12│MB13│
│ UV row 0 (interlv)  │                 ├────┼────┼────┼────┤
│ UV row 1            │                 │ ...                     │
└─────────────────────┘                 └────┴────┴────┴────┘
   按行连续，CPU 遍历友好                   按宏块排列，VPU 编码友好
```

**tiled 格式各厂商私有**（高通的 `Tile2m8ka`、MTK/三星的特定 Block 格式），Android 框架没有公开 API 让你访问。`ImageReader` / `onPreviewFrame` 拿到的永远是 linear。

#### 2.2.2 为什么 Tiled 在硬件编码中碾压级地快

两者在纯算力消耗上差不多，但硬件编码极度依赖**内存带宽**，Tiled 通过契合 VPU/GPU 的缓存机制，把内存乱序访问降到最低。

**核心原因：运动估计和帧内预测的高度局部性**

硬件编码器（VPU）在做 H.264/H.265 压缩时，两个最核心的操作——**帧内预测（Intra Prediction）**和**帧间运动估计（Motion Estimation）**——永远以宏块（16×16 / 8×8）为单位，频繁比对上下左右邻居像素。

**Linear 排布的致命弱点——Cache Miss 灾难：**

VPU 读一个 16×16 的 Linear 宏块：
1. 读第一行 16 像素 → 内存连续，OK
2. 读第二行 → **必须跳转内存地址**（跨过整行 `Stride`）
3. 第三行 → 再次跳转……

每次跨行跳转都触发 **Cache Miss**。在移动端 UMA 架构下，频繁 Cache Miss 迫使 VPU 直接去慢速 LPDDR（系统内存）抓数据 → **内存总线带宽被瞬间吃满、耗电飙升、编码掉帧**。

**Tiled 排布的杀手锏——Cache 全命中：**

VPU 读一个 16×16 的 Tiled 宏块：
1. 硬件发出一次内存读取指令
2. 整个 16×16 块在物理内存里**连续** → L1/L2 Cache **一次性预加载整个宏块**
3. 运动估计和残差计算的所有数据都在高速缓存里 → **访存延迟降低一到两个数量级**

> **Linear 是"每一行都要跑一趟图书馆"；Tiled 是"一次把整个宏块搬到自己书桌上"。**

#### 2.2.3 致命细节：MediaCodec 内部一定会把 Linear 转成 Tiled

这是一个很多开发者不知道的关键事实：**如果你用 ByteBuffer 模式把 Linear YUV 喂给 `MediaCodec` 硬件编码器，它内部一定会先转成 Tiled，再编码。** 硬件编码器绝对不会直接拿 Linear 数据跑运动估计算法——那会让芯片的硬件缓存彻底失效。

转换过程（发生在 `queueInputBuffer` 之后，你完全看不到）：

```text
ByteBuffer（Linear NV12）
    │
    │  queueInputBuffer()  ← 你在这里交数据
    ▼
OMX/C2 驱动层：
    │  ① 检测到这是堆内存/标准线性内存
    │  ② VPU 内部 DMA 引擎启动：逐行读出 Linear 像素
    │  ③ 按厂商私有瓦片算法（如高通 Tile2m8ka）重新排列
    │  ④ 写入 VPU 专属的 Tiled 硬件缓冲区
    ▼
VPU 计算核心：读到的是 Tiled → 开始 H.264 编码
```

**这个转换带来的三个代价：**

| 代价 | 说明 | 1080p 影响 | 4K 影响 |
|---|---|---|---|
| **内存带宽被吃** | 虽然是 DMA 硬件做（比 CPU for 循环快），但仍属于物理内存级搬运 | 可能无感 | 4K@60fps 时直接把总线带宽打满 |
| **额外编码延迟** | 数据不是直送编码核心，必须在 Staging Buffer 里"洗一遍牌" | +1~3ms | +5~10ms，加剧待编码队列堆积 |
| **发热加剧** | 频繁跨内存区域搬运 → 手机发烫 → 温控降频 → 帧率雪崩 | 轻度 | **致命**——推流 10 分钟后从 30fps 掉到 5fps |

#### 2.2.4 Surface 路径：从出生就是 Tiled，全程零转换

当 `MediaCodec` 配置为 Surface 输入（`createInputSurface()`）时，底层通过 **Gralloc** 直接向 GPU 申请 GraphicBuffer。**系统和芯片厂商在分配这块内存的瞬间，就已经将其物理排布指定为 Tiled。**

```text
Camera HAL ──→ gralloc buffer（usage: HW_CAMERA_WRITE | HW_VIDEO_ENCODER）
                  │
                  │  分配时就是 Tiled 格式！
                  │  Camera ISP 写入 → OpenGL 美颜处理 → 编码器 DSP 直接读
                  │  全程同一个 gralloc handle，零拷贝，零格式转换
                  ▼
              VPU 计算核心
```

对比 ByteBuffer 路径：

```text
ImageReader ──→ byte[]（CPU 可读 → 一定是 Linear）
                  │
                  │  inputBuffer.put(yuvData)   ← heap→native 拷贝（~3MB）
                  │  queueInputBuffer 之后
                  │  OMX 内部: Linear → Tiled   ← DMA 转换（白耗带宽 + 延迟 + 发热）
                  ▼
              VPU 计算核心
```

**最快的全硬件闭环**（抖音/快手级方案）：

```text
[Camera 采集] ──→ 吐出 GPU Tiled 纹理 (Gralloc)
                      │
                      ▼
[OpenGL 美颜特效] ──→ 在 GPU 内部直接处理 Tiled 纹理 (零拷贝)
                      │
                      ▼
[MediaCodec 编码] ──→ VPU 直接吃 Tiled 纹理进行 H.264 压缩
                      │
                      ▼
[网络发送] ──→ H.264 字节流（已不是像素，而是压缩码流 → Linear 字节数组）
```

图像从出生到被压缩成 H.264 之前，**全程 Tiled 排布 + 全程留在 GPU/VPU 显存**。直到编码器吐出 H.264 压缩字节，才变成 CPU 可用的 Linear 字节交给 Socket 发走。

#### 2.2.5 既然 Tiled 这么快，为什么还要有 Linear

- **网络/文件只认 Linear**：Socket 发送、MP4 写入——接收方不认识你的私有 Tiled 格式
- **CPU 算法需要 Linear**：OpenCV、逐像素分析、截图导出——CPU 的 Hardware Prefetcher 是一维预测电路，Tiled 的跳跃地址会让它 Cache Miss 到崩
- **跨芯片的公约数**：不同供应商的 CPU/GPU/ISP 之间交换数据，最安全的通用格式就是 Linear

#### 2.2.6 输出侧为什么没有 tile 概念

`encodeLoop` 里 `getOutputBuffer()` 拿到的已经是 **H.264 Annex-B 码流**——一维字节序列：

```
00 00 00 01 67 ...   ← SPS
00 00 00 01 68 ...   ← PPS
00 00 00 01 65 ...   ← IDR 帧
00 00 00 01 41 ...   ← P 帧
```

所以 `outputBuffer.get(frameData)`（~50KB）只是 native→heap 拷贝，不涉及任何 pixel format 转换。

#### 2.2.7 三种通路代价总览

| | 输入方式 | linear→tiled 转换 | 输入 CPU 拷贝 | 输出拷贝 | 核心瓶颈 |
|---|---|---|---|---|---|
| **模式一** ByteBuffer + SAFE_COPY | `encodeNV12(byte[])` | ✅ OMX 内部 DMA 转（~1-3ms） | ~3 MB | ~50 KB | 输入侧转换 + 拷贝 |
| **模式二** ByteBuffer + ZERO_COPY | `encodeNV12(byte[])` | ✅ 同上（~1-3ms） | ~3 MB | 0 | 输入侧转换 + 拷贝（没变） |
| **模式三** Surface | GPU 直接渲染 | ❌ **从源头就是 Tiled** | 0 | 0-50 KB | 几乎为零 |

> **模式二只省输出侧的 ~50 KB 拷贝。模式三才是降维打击——连输入侧的 3 MB 拷贝 + DMA 格式转换 + 内存带宽浪费一起省了。做直播推流，能用 Surface 输入就绝对不要用 ByteBuffer。**


---

## 三、关键设计决策

### 3.1 为什么选 COLOR_FormatYUV420Flexible？

这是 MediaCodec 保证**所有编码器接受**的唯一的颜色格式 key。不指定具体格式（如 NV12），由编码器自己适配。如果你指定了 COLOR_FormatYUV420SemiPlanar（NV12），在某些设备上直接 configure 失败。

### 3.2 为什么 Android 编码输出是 Annex-B？

Android 的 MediaCodec 编码器输出自带 `00 00 00 01` 起始码、SPS/PPS 作为 NALU 内联在流里——完全是标准 Annex-B。**所以 Android 推流不需要做 AVCC→Annex-B 转换**——这是和 iOS 最大的区别。推流时只需从 IDR 帧的 csd-0/csd-1 取出 SPS/PPS，确认它们已经在流里（通常 IDR 前面的 NALU 就是 SPS 和 PPS）。

### 3.3 实时编码的关键属性

| 属性 | 值 | 作用 |
|------|-----|------|
| KEY_BITRATE_MODE | BITRATE_MODE_CBR | 固定码率，防止瞬时流量冲爆 |
| KEY_LATENCY (API 26+) | 1 | 低延迟模式，等价 x264 `-tune zerolatency` |
| KEY_I_FRAME_INTERVAL | fps×2 | 短 GOP(2s)，快速随机切换 |
| KEY_PROFILE | AVCProfileBaseline | 兼容性最好，不含 B 帧 |
| KEY_PRIORITY (API 23+) | 0 (realtime) | 实时优先级 |

---

## 四、常见坑

### 坑 1：Surface 输入模式和 ByteBuffer 不兼容

`codec.configure()` 时如果传了 input Surface，就不能再 dequeueInputBuffer/getInputBuffer。两条路只能选一条。

### 坑 2：csd-0/csd-1 对编码器不能用

csd-0/csd-1 是**解码器**的参数——configure 时告诉解码器 SPS/PPS 是什么。**编码器**不接受 csd——你传了会 configure 失败。编码器**自产** SPS/PPS。

### 坑 3：某些设备不支持 CBR 模式

`codecInfo.getCapabilitiesForType().isBitrateModeSupported(CBR)` 先查询不支持则降级到 VBR。

### 坑 4：dequeueOutputBuffer 返回 INFO_OUTPUT_FORMAT_CHANGED

编解码器启动后必然发生一次——表示输出格式已确定。**只有这之后才能拿到有效的 output buffer**。很多新手不知道要处理这个返回值。

---

## 五、附录：AHardwareBuffer 深度解析 —— 零拷贝的物理基石

> 2.2 节讲了 tiled/linear 和 Surface 路径为什么快。这一节深入 AHardwareBuffer 本身——它是怎么做到「传指针不搬数据」的。

### 5.1 AHardwareBuffer 是什么

不是普通的 `void*` 指针，更不是物理内存地址。它的本质是 **Linux DMA-BUF 的句柄封装**——一个跨进程、跨硬件的内存描述符（File Descriptor）：

1. Gralloc 在物理 RAM 里锁住一块共享内存
2. 内核把这块内存打包成一个 **FD（文件描述符）**
3. 传递时只传这个轻量 FD——不是拷贝 30MB 像素
4. 目标进程/硬件拿到 FD 后，通过 `mmap` 将物理内存映射到自己的虚拟地址空间

**生命周期**：FD 使用内核引用计数——只要还有人持有 FD，物理内存就不释放；全部释放后内核自动回收。

### 5.2 USAGE 标志位：和系统「谈判」

`AHardwareBuffer_allocate` 时你必须声明哪些硬件要用这块内存。底层的 **Gralloc** 模块会根据 USAGE 去协调各硬件驱动，找到它们共同支持的最优私有格式。

**核心 USAGE 枚举：**

| 标志 | 含义 |
|---|---|
| `GPU_FRAMEBUFFER` | GPU 可作为渲染输出目标（FBO color attachment） |
| `GPU_SAMPLED_IMAGE` | GPU 可作为纹理采样 |
| `VIDEO_ENCODE` | 硬件编码器可直接读取并编码 |
| `CPU_READ_OFTEN` | ⚠️ 危险标志——见下 |
| `CPU_WRITE_OFTEN` | CPU 会频繁写入 |

**三种黄金组合：**

| 模式 | 组合 | 场景 |
|---|---|---|
| **GPU 内部流转** | `GPU_FRAMEBUFFER \| GPU_SAMPLED_IMAGE` | 渲染 Pass A 输出 → Pass B 纹理输入，不经过 CPU |
| **硬件直通编码** | `GPU_FRAMEBUFFER \| VIDEO_ENCODE` | GPU 渲染完 → VPU 直接编码，直播终极形态 |
| **CPU 生成 → GPU 消费** | `CPU_WRITE_OFTEN \| GPU_SAMPLED_IMAGE` | CPU 解出画面/画 UI，喂给 GPU 当纹理 |

### 5.3 💣 永远不要盲目加 `CPU_READ_OFTEN`

一旦加上 `CPU_READ_OFTEN`，Gralloc 为了照顾 CPU 的线性读取能力，会**一票否决**所有私有块状格式和硬件压缩方案（AFBC/UBWC），整块内存强制降级为最原始的 **Linear 线性布局**。

后果链：GPU 纹理采样 Cache 命中率暴跌 → AFBC/UBWC 压缩失效 → 内存带宽被榨干 → 手机发烫 → 温控降频 → 帧率雪崩。

**只有当你真的需要在 CPU 端逐像素处理数据（如 OpenCV、截图导出）时，才可谨慎使用。**

### 5.4 物理原理：为什么 CPU 和 GPU 不能共用同一种内存布局

两者内部的**物理缓存硬件**设计理念完全不同：

| | CPU | GPU |
|---|---|---|
| 缓存层级 | L1→L2→L3 深三级私有/共享 | L1 Texture Cache + L2 全局 Cache，扁平但带宽恐怖 |
| 预取器 | **一维线性预测**——探测到顺序访问模式后提前搬 Cache Line | **二维空间感知**——纹理单元知道采样坐标 (x,y)，直接拉周围邻域像素进 Cache |
| 友好布局 | Linear（行连续） | Tiled（块状 Morton 顺序） |
| 为什么？ | `array[i]` 地址连续 → Cache Line 高效 | 双线性采样需要 4 个邻域像素 → tiled 让它们物理上紧挨着，一把抓进 Texture Cache |

**这就是物理定律：CPU 的 Hardware Prefetcher 是一维预测电路，GPU 的 Texture Fetch Unit 是二维空间批量加载器。你的内存布局必须匹配访问者的硬件特性。**

---

## 一句话总结
> Android MediaCodec 编码 = COLOR_FormatYUV420Flexible + BITRATE_MODE_CBR + dequeue/queue 循环 + 输出已是 Annex-B（和 iOS 相反，无需格式转换）。Surface 输入零拷贝更优。

## 关联文档
- [[../ffmpeg/14-Android硬件编解码]] — MediaCodec 全文深讲
- [[01-Camera2采集详解]] — 编码输入的 YUV 来源
- [[04-OpenGLES渲染与Surface详解]] — Surface 输入零拷贝编码
- [[06-端到端采集编码推流管线]] — 完整推流链路
