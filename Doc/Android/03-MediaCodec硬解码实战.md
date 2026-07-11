# MediaCodec 硬解码实战：从 H.264 比特流到渲染

> **适用方向**：Android 播放器 / RTC 拉流 / 本地视频解码
> **前置知识**：MediaCodec 基础、H.264 NALU / Annex-B 格式
> **难度**：⭐⭐⭐（1-5 星）
> **预计阅读**：速记 8 分钟｜全文 25 分钟
> **关联文档**：[[00-Android音视频开发全景导读]] · [[../ffmpeg/14-Android硬件编解码]] · [[04-OpenGLES渲染与Surface详解]]
> **定位**：🟢 中级必会 —— Android 端拉流/播放的核心

---

## 一、面试速记

| # | 考点 | 一句话答案 |
|---|------|-----------|
| 1 | 解码器创建和编码器有什么不同 | configure 时要传 csd-0/csd-1（SPS/PPS），不需要 CONFIGURE_FLAG_ENCODE |
| 2 | 解码输入格式 | Annex-B（00 00 00 01 起始码），每帧一个 NALU 或完整 AU |
| 3 | 解码输出有几种方式 | Surface（零拷贝直接渲染）/ ByteBuffer + Image（CPU 读像素） |
| 4 | 解码花屏咋办 | 颜色格式 + stride + sliceHeight 三个一起查，优先切换 Surface 输出 |
| 5 | SPS/PPS 在哪传 | codec.configure() 的 MediaFormat 里设 "csd-0"=SPS 和 "csd-1"=PPS |
| 6 | 为什么推荐 Surface 输出 | 零拷贝 → 无颜色格式问题 → 无 stride 问题 → 一条龙省心 |

---

## 二、核心 Demo：Surface 零拷贝解码器

```java
// MediaCodecDecoder.java
// 完整的 Android MediaCodec H.264 解码器 (Surface 零拷贝输出)
package com.example.decoder;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaFormat;
import android.os.Handler;
import android.os.HandlerThread;
import android.view.Surface;

import java.nio.ByteBuffer;

public class MediaCodecDecoder {
    public interface DecoderCallback {
        /** 解码格式确定 (分辨率/SPS/PPS 等) */
        void onFormatReady(int width, int height);
        /** 输出 Surface 上有新帧 (如果是 Surface 模式) */
        void onFrameAvailable();
    }

    private MediaCodec codec;
    private HandlerThread decoderThread;
    private Handler decoderHandler;
    private DecoderCallback callback;
    private Surface outputSurface; // 如果是 Surface 输出模式
    private boolean surfaceMode;

    // ============================================================
    // 创建解码器
    // ============================================================
    public MediaCodecDecoder(Surface outputSurface, DecoderCallback callback) {
        this.outputSurface = outputSurface;
        this.surfaceMode = (outputSurface != null);
        this.callback = callback;
        decoderThread = new HandlerThread("DecoderThread");
        decoderThread.start();
        decoderHandler = new Handler(decoderThread.getLooper());
    }

    public void start(byte[] sps, byte[] pps, int width, int height) {
        try {
            String mime = MediaFormat.MIMETYPE_VIDEO_AVC;
            MediaCodecInfo codecInfo = selectCodec(mime);
            codec = MediaCodec.createByCodecName(codecInfo.getName());

            MediaFormat format = MediaFormat.createVideoFormat(mime, width, height);

            // ★ 解码器关键: csd-0=SPS, csd-1=PPS
            // 从 Annex-B 流里提取的 SPS/PPS 要**去掉起始码**后传入
            ByteBuffer csd0 = ByteBuffer.wrap(sps); // 纯 SPS, 无起始码
            ByteBuffer csd1 = ByteBuffer.wrap(pps); // 纯 PPS, 无起始码
            format.setByteBuffer("csd-0", csd0);
            format.setByteBuffer("csd-1", csd1);

            codec.configure(format, outputSurface, null, 0);
            //                        ↑ Surface 输出: 零拷贝
            //                        ↑ null:     ByteBuffer 输出: CPU 可读
            codec.start();

            decoderHandler.post(this::decodeLoop);
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    // ============================================================
    // 喂入 Annex-B H.264 数据
    // ============================================================
    public void decodeAnnexBFrame(byte[] annexBData, long ptsUs, boolean isKeyFrame) {
        if (codec == null) return;

        int inputIndex = codec.dequeueInputBuffer(10000);
        if (inputIndex < 0) return;

        ByteBuffer inputBuffer = codec.getInputBuffer(inputIndex);
        inputBuffer.clear();
        inputBuffer.put(annexBData);

        int flags = isKeyFrame ? MediaCodec.BUFFER_FLAG_SYNC_FRAME : 0;

        codec.queueInputBuffer(inputIndex, 0, annexBData.length, ptsUs, flags);
    }

    // ============================================================
    // 解码循环
    // ============================================================
    private void decodeLoop() {
        MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();

        while (codec != null) {
            int outputIndex = codec.dequeueOutputBuffer(info, 10000);

            if (outputIndex >= 0) {
                if (surfaceMode) {
                    // ★ Surface 模式: releaseOutputBuffer 会自动渲染到 outputSurface
                    // 第 2 个参数 render=true → 直接推到 Surface 上显示（零拷贝）
                    codec.releaseOutputBuffer(outputIndex, true);
                    if (callback != null) callback.onFrameAvailable();
                } else {
                    // ★ ByteBuffer 模式: CPU 读像素（有坑！见下文）
                    // Image image = codec.getOutputImage(outputIndex);
                    // processImage(image);
                    codec.releaseOutputBuffer(outputIndex, false);
                }
            }
            else if (outputIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                // ★ 格式确定: 可以拿分辨率
                MediaFormat fmt = codec.getOutputFormat();
                int w = fmt.getInteger(MediaFormat.KEY_WIDTH);
                int h = fmt.getInteger(MediaFormat.KEY_HEIGHT);
                if (callback != null) callback.onFormatReady(w, h);
            }
        }
    }

    // ============================================================
    // 选择解码器（硬件优先）
    // ============================================================
    private MediaCodecInfo selectCodec(String mime) {
        MediaCodecList list = new MediaCodecList(MediaCodecList.ALL_CODECS);
        for (MediaCodecInfo info : list.getCodecInfos()) {
            if (info.isEncoder()) continue;
            for (String type : info.getSupportedTypes()) {
                if (type.equalsIgnoreCase(mime)) {
                    // 优先硬件
                    String name = info.getName().toLowerCase();
                    if (name.contains("omx.qcom") || name.contains("omx.mtk") ||
                        name.contains("c2.qti")  || name.contains("c2.mtk")) {
                        return info;
                    }
                }
            }
        }
        return list.getCodecInfos()[0];
    }

    public void stop() {
        if (codec != null) { codec.stop(); codec.release(); codec = null; }
        if (decoderThread != null) decoderThread.quitSafely();
    }
}
```

---

## 三、使用示例

```java
// ---- 初始化 ----
// 用 SurfaceView/TextureView 的 Surface 作为输出
Surface outputSurface = textureView.getSurfaceTexture() != null
    ? new Surface(textureView.getSurfaceTexture()) : null;

MediaCodecDecoder decoder = new MediaCodecDecoder(outputSurface,
    new MediaCodecDecoder.DecoderCallback() {
        @Override public void onFormatReady(int w, int h) {
            // 解码器已就绪，可以开始渲染
        }
        @Override public void onFrameAvailable() {
            // Surface 模式回调（可选，TextureView 有自己回调）
        }
    });

// ---- 从第一个 IDR 提取 SPS/PPS ----
byte[] sps = extractNaluByType(idrData, 7); // SPS
byte[] pps = extractNaluByType(idrData, 8); // PPS
decoder.start(sps, pps, 1920, 1080);

// ---- 循环解码 ----
while (receiving) {
    byte[] annexBFrame = receiveAnnexBFrame();
    boolean isKeyFrame = (annexBFrame[4] & 0x1F) == 5; // IDR
    decoder.decodeAnnexBFrame(annexBFrame, ptsUs, isKeyFrame);
}
```

---

## 四、ByteBuffer 模式 vs Surface 模式

| 维度 | ByteBuffer + Image | Surface |
|------|-------------------|---------|
| 数据位置 | CPU 可读（需逐 plane 拷贝） | GPU 显存（零拷贝） |
| 颜色格式坑 | **严重**（NV12/NV21/I420/tiled） | **无**（GPU 内部处理） |
| stride 处理 | 必须手动 rowStride/pixelStride | GPU 自动处理 |
| 后处理（美颜滤镜） | 拿到 YUV 后可自定义 | 需在 GL 层面后处理 |
| 性能 | CPU memcpy 每帧 ~3-5ms | ~0ms |

**结论**：纯播放走 Surface；需要截帧/美颜/AI 分析走 ByteBuffer + Image。

---

## 五、常见坑

### 坑 1：csd-0/csd-1 带了起始码

`MediaFormat.setByteBuffer("csd-0", ...)` 要求传**纯 SPS 数据**（去掉 00 00 00 01）。传了起始码 → configure 失败。正确做法是先扫 Annex-B 流，找 type=7(SPS)/8(PPS) NALU，去掉起始码，传纯 NALU 数据。

### 坑 2：花屏 → 换 Surface 输出立刻正常

ByteBuffer 模式在设备 A 正常、设备 B 花屏 → 因为设备 B 的输出颜色格式是私有 tiled 格式，CPU 无法正确解析。**最快修复：换成 Surface 输出**。如果必须 CPU 读，用 `getOutputImage()` 的 Image 接口 + rowStride。

### 坑 3：EOS 之后继续 dequeue 会卡死

**一句话：`BUFFER_FLAG_END_OF_STREAM` 是"这顿饭上完了"的信号——解码器吃完最后一口，吐出最后一帧，给 output 队列塞一个 EOS 标记，然后进入 EOS 状态。此时再 dequeue 就会永远等下去，因为不会再有任何数据了。**

#### 原理详解

MediaCodec 的 EOS（End Of Stream）是一个**输入→穿越→输出**的完整生命周期：

```
第一步（输入侧）：你向编码器/解码器发出终止信号
    int inputIndex = codec.dequeueInputBuffer(timeout);
    codec.queueInputBuffer(inputIndex, 0, 0, 0,
        MediaCodec.BUFFER_FLAG_END_OF_STREAM);
    //                          ↑
    //          通知编解码器：我不会再给你任何数据了

第二步（内部处理）：编解码器收到 EOS 信号后
    - 停止等待新的输入数据
    - 把内部缓冲区里剩余的数据全部处理完（flush 剩余帧）
    - 把所有剩余的 output frame 排入输出队列
    - 在输出队列末尾插入一个特殊的 EOS 标记帧

第三步（输出侧）：你从输出端感知 EOS
    int outputIndex = codec.dequeueOutputBuffer(info, timeout);
    if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
        // ← 这是最后一个信号！
        // 收到后，不要再 dequeue 了——后面什么都没有了
        codec.releaseOutputBuffer(outputIndex, false);
        codec.stop();
        codec.release();
        break;  // ← 退出循环
    }
```

#### 为什么继续 dequeue 会卡死？

MediaCodec 的状态机决定了：EOS 标记从输出端取出后，编解码器知道"发送端已经确认收到了终止信号"，正式进入**完全停止状态**。此后：

- 不会再产生任何新的 output
- 不会再有任何 input buffer 可用
- `dequeueOutputBuffer(timeout)` 永远等不到下一个 buffer

如果你的 timeout 设了 `-1`（无限等）或一个正值，就**永久阻塞**在当前线程上，形成死等。如果 timeout 是 0，会在超时后返回 `INFO_TRY_AGAIN_LATER`，不会卡死但会空转浪费 CPU。

#### 正确的关闭流程

```java
// ① 发送 EOS
int inIdx = codec.dequeueInputBuffer(10000);
if (inIdx >= 0) {
    codec.queueInputBuffer(inIdx, 0, 0, 0,
        MediaCodec.BUFFER_FLAG_END_OF_STREAM);
}

// ② 循环接收 output，直到收到 EOS
MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
while (true) {
    int outIdx = codec.dequeueOutputBuffer(info, 10000);

    if (outIdx >= 0) {
        if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
            // ★ 收到 EOS → 立即退出循环，不再 dequeue
            codec.releaseOutputBuffer(outIdx, false);
            break;
        }
        // 正常帧：渲染/处理
        codec.releaseOutputBuffer(outIdx, true); // Surface 模式
    }
}

// ③ EOS 已确认 → 安全停止
codec.stop();
codec.release();
```

#### 编码器端的差异

编码器同样使用 `BUFFER_FLAG_END_OF_STREAM`，但含义略有不同：
- **编码器收到 EOS**：意味着"不会再有新的输入帧"，编码器会把内部剩余的帧全部编完输出，最后在 output 端放一个 EOS 标记
- 编码器的 EOS 必须在**所有数据都 `queueInputBuffer` 之后**单独发送一次，不能在最后一帧数据上直接带 `BUFFER_FLAG_END_OF_STREAM` 标志（因为最后一帧仍然是有效数据帧，不是信号帧）

#### 最容易犯的错误

```java
// ❌ 错误：收到 EOS 后没有 break，继续循环
while (true) {
    int outIdx = codec.dequeueOutputBuffer(info, 0); // timeout=0，不会死
    if (outIdx >= 0) {
        boolean isEos = (info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
        codec.releaseOutputBuffer(outIdx, false);
        // 忘记 break！下次循环 dequeue 返回 TRY_AGAIN_LATER → 空转
    }
}

// ❌ 更糟：timeout=-1 时忘记 break
while (true) {
    int outIdx = codec.dequeueOutputBuffer(info, -1); // 无限等
    if (outIdx >= 0) {
        // 收到 EOS 后没有 break
        // 下次 dequeueOutputBuffer(-1) → 永久阻塞，线程卡死！
    }
}
```

#### 一句话记忆

> EOS 是单向信号——你发一次（input），编解码器透传一次（内部），你收一次（output）。收到之后水面下就什么都没有了，再伸手 `dequeue` 就是把手伸进空池子——timeout=0 空转，timeout=-1 死等。

---

## 六、硬件解码底层原理：AVFrame 与输出帧管理

> 软解时 `AVFrame` 装的是「像素数据」；硬解时 `AVFrame` 装的是「硬件资源的引用」。搞混这一点，是硬解 Bug 的最大来源。

### 6.1 软解 vs 硬解：AVFrame 里到底装了什么

| 维度 | 软解 `AVFrame` | 硬解 `AVFrame` |
|---|---|---|
| `frame->format` | `YUV420P` / `NV12` / `P010` 等普通像素格式 | `AV_PIX_FMT_VIDEOTOOLBOX` / `D3D11` / `CUDA` / `AHARDWAREBUFFER` 等硬件格式 |
| `data[]` 的含义 | 通常就是像素平面地址 | 往往不是像素地址，可能是平台对象、纹理、surface、设备内存指针 |
| `hw_frames_ctx` | 通常为空 | 非常关键——描述硬件上下文、设备类型、底层软件格式 |
| 使用方式 | 直接读像素平面 | 先识别硬件类型 → 提取底层资源 → 决定走 GPU 还是转 CPU |

**拿到硬解 AVFrame 三步判断法：**

1. **看 `format`**：是普通软件格式还是硬件格式
2. **看资源类型**：装的是 `CVPixelBuffer` / `AHardwareBuffer*` / `ID3D11Texture2D*` / CUDA device pointer / QSV surface
3. **决定走向**：继续走 GPU（不转软件帧）还是回 CPU（做硬件帧→软件帧 transfer）

### 6.2 各平台硬解 AVFrame 速查

| 平台 | `frame->format` | 真正装的东西 | GPU-only 用法 |
|---|---|---|---|
| **Apple** | `AV_PIX_FMT_VIDEOTOOLBOX` | `data[3]` = `CVPixelBufferRef` | `CVMetalTextureCache` / `CVOpenGLESTextureCache` → 纹理 |
| **Android** | `AV_PIX_FMT_AHARDWAREBUFFER` | `data[3]` = `AHardwareBuffer*` | → `EGLImage` → GL texture / Vulkan image |
| **Windows D3D11** | `AV_PIX_FMT_D3D11` | `data[0]` = `ID3D11Texture2D*`, `data[1]` = subresource | 直接 D3D 渲染 |
| **Windows/Linux CUDA** | `AV_PIX_FMT_CUDA` | `data[0/1]` = device memory 平面指针, `hw_frames_ctx` 含 CUDA context | CUDA-OpenGL/ Vulkan 互操作 |
| **Intel QSV** | `AV_PIX_FMT_QSV` | QSV surface, 核心信息在 `hw_frames_ctx` | 先桥接到 D3D11 再渲染 |

### 6.3 为什么 Android 硬解比其他平台更复杂

Android `MediaCodec` 的原始设计哲学是**「解码器直接把数据推到 Surface 上屏，不要让 CPU 碰到数据」**。如果你想让 CPU 拿到数据或做自定义 OpenGL 渲染，需要绕很远的路（`SurfaceTexture` + `OES Texture`，或 Android 8.0+ 的 `AImageReader` / `AHardwareBuffer`）。这导致：

- **初始化阶段是重头戏**：必须在 `configure` 时就协商好数据管线（Surface 模式 vs ByteBuffer 模式），一旦启动就不能改
- **厂商 HAL 碎片化严重**：Kirin 芯片 `flush` 可能死锁（需先发 EOF 骗它排空），MTK 芯片遇 B 帧可能跳帧（需改写 SPS 头），逼着开发者在各生命周期节点埋 workaround
- **对比 Apple**：`VideoToolbox` 输出统一是 `CVPixelBuffer`，初始化极简，复杂逻辑全在拿到帧之后

### 6.4 硬解输出帧数量是有限的

硬解输出帧对应底层**有限的硬件资源槽位**（surface pool / output buffer），通常由驱动和解码器配置决定：

| 平台 | 常见输出帧数 |
|---|---|
| Android MediaCodec 低时延 | 4~8 |
| Android MediaCodec 普通 | 6~12 |
| iOS VideoToolbox 低时延 | 3~6 |
| Windows D3D11 | 6~12 |
| NVIDIA NVDEC | 8~20 |

**关键影响：如果上层一直持有输出帧不释放，解码器输出池被占满 → 解码器阻塞 → 上游反压 → 整条 pipeline 卡住。**

### 6.5 未解码的 I/B/P 占用这些槽位吗？

- **不直接占用**。未解码的 I/B/P 仍是压缩码流，占用的是 **输入队列 / packet buffer**。
- 真正占输出资源的是：**已解码但未输出的帧、作为参考帧保留的帧、已输出但上层未释放的帧**。
- B 帧**间接**增加资源需求——因为它增加参考帧保留和显示重排需求，抬高解码器对 surface 数量的要求。这也是为什么低时延场景要尽量关 B 帧。

### 6.6 surface 和 buffer 这两个词怎么区分

- **buffer**：偏「资源容器」，可装压缩码流也可装解码图像
- **surface**：偏「图像帧载体」，通常有宽高、像素格式、stride，可被 GPU/显示模块直接使用
- 面试说法：「在硬解场景里，两者通常指向同一类底层输出资源，buffer 强调容器语义，surface 强调图像语义。」

---

## 🎯 一句话总结

> Android 解码 = csd-0/csd-1 传 SPS/PPS → Annex-B 喂入 → Surface 零拷贝输出最省心（无颜色格式/stride 坑）。ByteBuffer 输出必须用 Image API + rowStride/pixelStride 逐 plane 正确拷贝。

## 🔗 关联文档

- [[../ffmpeg/14-Android硬件编解码]] — MediaCodec 全文深讲（颜色格式 § 详细展开）
- [[04-OpenGLES渲染与Surface详解]] — SurfaceTexture + OpenGL ES 渲染
- [[06-端到端采集编码推流管线]] — 拉流解码完整管线
