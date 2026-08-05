# 25 - 硬解码到渲染流程：MediaCodec / VideoToolbox

## 0. 本篇定位

| 项 | 说明 |
|---|---|
| 面试位置 | 硬解码到渲染链路专题：回答“硬解出来的帧怎么显示、怎么做到零拷贝”。 |
| 先背什么 | Android 走 `MediaCodec -> Surface/SurfaceTexture -> OpenGL ES/Vulkan`，iOS 走 `VideoToolbox -> CVPixelBuffer/IOSurface -> Metal/CoreAnimation`；核心是让帧留在硬件/GPU 可访问内存里。 |
| 深入怎么学 | 先理解 CPU 内存帧和硬件帧的差异，再看 buffer 生命周期、同步点、格式转换和何时发生隐式拷贝。 |
| 关联阅读 | 07、10、14、15、16 |

---

## 一、面试速答模板

如果面试官问“硬解码后的画面怎么渲染，怎么做到零拷贝”，可以这样答：

> 硬解到渲染的关键不是“用了硬解 API”本身，而是解码输出的图像有没有被下载回 CPU。Android 上通常创建一个 `Surface` 作为 `MediaCodec` 的输出目标，解码器把图像写到系统图形缓冲区，渲染侧通过 `SurfaceTexture`、`ANativeWindow`、OpenGL ES 或 Vulkan 消费这块 buffer；iOS/macOS 上 `VideoToolbox` 解码输出 `CVPixelBuffer`，底层常由 `IOSurface` 承载，再通过 `CVMetalTextureCache` 变成 Metal texture。只要中间不调用类似 `ImageReader` 读像素、`glReadPixels`、`CVPixelBufferLockBaseAddress` 后拷贝数据，画面就基本保持在硬件/GPU 路径上。真正的工程难点是 buffer 生命周期、格式兼容、颜色空间、线程同步和什么时候会发生隐式拷贝。

这个回答要带出四个点：

| 点 | 面试官想听什么 |
|---|---|
| 数据在哪里 | CPU 内存、GPU/硬件 buffer、系统图形 buffer，不是一回事 |
| 谁生产谁消费 | 解码器生产，渲染器消费，中间靠 Surface / CVPixelBuffer 传递所有权或引用 |
| 零拷贝边界 | 不下载、不读回、不转成普通 `uint8_t*`，尽量让图像作为 texture 继续走 |
| 工程坑 | stride、颜色格式、YUV/RGB 转换、同步、buffer 释放、设备兼容 |

---

## 二、为什么“硬解码”不等于“零拷贝”

很多人以为只要开了硬解码，整条链路就一定快。实际不是。

```text
压缩码流 AVPacket / NALU
        |
        v
硬件解码器
        |
        +-- 输出到 CPU 可读内存       -> 方便处理，但可能发生下载/拷贝
        |
        +-- 输出到硬件/图形缓冲区     -> 适合渲染/再编码/硬件滤镜，才是零拷贝方向
```

硬解码只说明“解码计算”由硬件模块完成；零拷贝还要求“解码后的大图像”不被搬回 CPU。

一帧 1080p NV12 大约 3MB，4K NV12 大约 12MB。60fps 下如果每帧都下载、转换、再上传，CPU、内存带宽和总线都会被打爆。中高级面试里，要把这层代价讲出来。

---

## 三、Android：MediaCodec 到渲染

Android 最常见的硬解渲染链路有两种。

### 3.1 Surface 输出模式：生产推荐

```text
MediaExtractor / 网络接收
        |
        v
H.264/H.265 NALU + CSD(SPS/PPS/VPS)
        |
        v
MediaCodec.configure(format, outputSurface, null, 0)
        |
        v
MediaCodec.decode
        |
        v
Surface / BufferQueue / GraphicBuffer
        |
        v
SurfaceTexture -> OpenGL ES OES texture
        |
        v
shader 做 YUV/RGB 采样、旋转、裁剪、显示
```

关键点：

| 环节 | 要点 |
|---|---|
| `Surface` | 作为解码器输出目标，解码后的图像进入系统图形缓冲区 |
| `BufferQueue` | 解码器是 producer，渲染线程是 consumer |
| `SurfaceTexture` | 把外部图像暴露成 `GL_TEXTURE_EXTERNAL_OES` |
| `updateTexImage()` | 获取下一帧，同时更新纹理和 transform matrix |
| shader | 处理颜色转换、旋转、镜像、裁剪，不把数据读回 CPU |

典型调用顺序：

```java
MediaFormat format = MediaFormat.createVideoFormat(mime, width, height);
format.setByteBuffer("csd-0", spsBuffer);
format.setByteBuffer("csd-1", ppsBuffer);

SurfaceTexture surfaceTexture = new SurfaceTexture(oesTextureId);
Surface outputSurface = new Surface(surfaceTexture);

MediaCodec codec = MediaCodec.createDecoderByType(mime);
codec.configure(format, outputSurface, null, 0);
codec.start();

// 输入压缩数据
int inIndex = codec.dequeueInputBuffer(timeoutUs);
ByteBuffer input = codec.getInputBuffer(inIndex);
input.put(naluOrSample);
codec.queueInputBuffer(inIndex, 0, size, ptsUs, flags);

// 输出时 releaseOutputBuffer(index, true) 表示把帧渲染到 Surface
MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
int outIndex = codec.dequeueOutputBuffer(info, timeoutUs);
if (outIndex >= 0) {
    codec.releaseOutputBuffer(outIndex, true);
    surfaceTexture.updateTexImage();
    surfaceTexture.getTransformMatrix(texMatrix);
    drawOesTexture(oesTextureId, texMatrix);
}
```

面试追问：“为什么 `releaseOutputBuffer(index, true)` 之后才显示？”

因为输出 buffer 还在 codec 手里。`releaseOutputBuffer(index, true)` 的含义是：这块解码完成的图形 buffer 可以提交给绑定的 `Surface`，进入渲染侧的 BufferQueue。没有 release，consumer 拿不到这帧；release 太慢，解码器也会因为可用输出 buffer 不够而卡住。

### 3.2 ByteBuffer / Image 输出模式：适合 CPU 处理，不适合纯渲染

```text
MediaCodec 输出 ByteBuffer / Image
        |
        v
CPU 读取 YUV plane
        |
        v
自己转换/算法处理/再上传纹理
```

这种方式适合截图、CPU 算法、软件滤镜、测试 dump，但不适合高性能播放。因为你已经把帧从硬件/图形路径拉回 CPU 了，之后如果还要显示，又要上传回 GPU。

典型坑：

| 坑 | 现象 | 根因 |
|---|---|---|
| 忽略 `Image.Plane.rowStride` | 画面斜、花屏 | 每行有 padding，不能按 width 拷贝 |
| 忽略 `pixelStride` | UV 错位、偏色 | NV12/NV21/厂商私有格式不是简单 I420 |
| 用 `ImageReader` 截每一帧 | 播放掉帧、发热 | 每帧 CPU 读回，破坏零拷贝 |
| 只看 `COLOR_FormatYUV420Flexible` | 仍然花屏 | flexible 是抽象承诺，具体 stride/pixelStride 还要查 |

---

## 四、iOS/macOS：VideoToolbox 到渲染

iOS/macOS 的典型链路：

```text
Annex-B / AVCC H.264/H.265 码流
        |
        v
CMVideoFormatDescription
        |
        v
VTDecompressionSessionDecodeFrame
        |
        v
CVPixelBuffer
        |
        v
IOSurface-backed pixel buffer
        |
        v
CVMetalTextureCacheCreateTextureFromImage
        |
        v
Metal texture -> shader -> drawable
```

### 4.1 解码侧核心对象

| 对象 | 作用 |
|---|---|
| `CMVideoFormatDescription` | 描述编码格式、宽高、SPS/PPS/VPS 等参数集 |
| `CMSampleBuffer` | 包装一帧压缩样本、时间戳和 format description |
| `VTDecompressionSession` | 硬解码会话 |
| `CVPixelBuffer` | 解码后的图像帧载体 |
| `IOSurface` | 可跨进程/跨框架共享的底层图形内存对象 |

### 4.2 Metal 渲染路径

典型思路不是把 `CVPixelBuffer` 拷贝成普通内存，而是把它转成 Metal texture：

```objective-c
CVPixelBufferRef pixelBuffer = imageBuffer;

CVMetalTextureRef yTextureRef = NULL;
CVMetalTextureRef uvTextureRef = NULL;

CVMetalTextureCacheCreateTextureFromImage(
    kCFAllocatorDefault,
    textureCache,
    pixelBuffer,
    NULL,
    MTLPixelFormatR8Unorm,
    width,
    height,
    0,
    &yTextureRef);

CVMetalTextureCacheCreateTextureFromImage(
    kCFAllocatorDefault,
    textureCache,
    pixelBuffer,
    NULL,
    MTLPixelFormatRG8Unorm,
    width / 2,
    height / 2,
    1,
    &uvTextureRef);

id<MTLTexture> yTexture = CVMetalTextureGetTexture(yTextureRef);
id<MTLTexture> uvTexture = CVMetalTextureGetTexture(uvTextureRef);
```

如果输出格式是 NV12，通常 plane 0 是 Y，plane 1 是 UV。shader 里采样 Y 和 UV，再做 YUV 到 RGB 转换。

### 4.3 iOS 上容易发生拷贝的动作

| 动作 | 影响 |
|---|---|
| `CVPixelBufferLockBaseAddress` 后 CPU 扫整帧 | 可能触发同步或让 CPU 等 GPU/硬件写完 |
| 把 `CVPixelBuffer` 转成 `UIImage` | 通常发生格式转换和拷贝，只适合截图/调试 |
| `glReadPixels` / Metal readback | 从 GPU 读回 CPU，实时链路里代价很高 |
| 解码输出 BGRA 再渲染 | 方便但可能多一步色彩转换，视频播放通常 prefer NV12 |

---

## 五、Android vs iOS 横向对比

| 维度 | Android | iOS/macOS |
|---|---|---|
| 解码 API | `MediaCodec` / `AMediaCodec` | `VideoToolbox` |
| 常见零拷贝输出 | `Surface` | `CVPixelBuffer` |
| 底层共享对象 | `GraphicBuffer` / `AHardwareBuffer` / dma-buf | `IOSurface` |
| 渲染入口 | `SurfaceTexture`、OpenGL ES、Vulkan | Metal、CoreAnimation、OpenGL ES |
| 常见纹理形态 | `GL_TEXTURE_EXTERNAL_OES` | `MTLTexture` from `CVMetalTextureCache` |
| 典型颜色格式 | NV12/NV21/厂商格式/OES 外部纹理 | NV12、BGRA |
| 经典坑 | stride、pixelStride、CSD、设备碎片化 | AVCC/Annex-B、SPS/PPS、pixel buffer 属性、颜色矩阵 |

一句话记忆：Android 的关键对象是 `Surface`，iOS 的关键对象是 `CVPixelBuffer`；二者背后都在解决同一个问题：让解码器输出的图像能被渲染器直接消费。

---

## 六、什么时候必须下载到 CPU

零拷贝不是绝对信仰。下面这些场景可能必须或值得下载：

| 场景 | 说明 |
|---|---|
| CPU 算法只接受普通内存 | 老算法、第三方库、调试工具可能只吃 `uint8_t*` |
| 截图/缩略图 | 偶发操作可以接受拷贝，实时每帧不行 |
| 软件编码器 | 如果后面接 x264 这类 CPU 编码器，需要 CPU 内存帧 |
| 平台不支持目标硬件格式 | 兼容性兜底时可能要下载再转换 |
| 精确像素分析 | 质量评估、测试 dump、离线分析可以牺牲性能 |

回答时要体现取舍：实时播放/通话尽量零拷贝；离线处理、调试、兼容兜底可以接受拷贝，但要知道代价。

---

## 七、常见排查表

| 现象 | 优先怀疑 | 排查方向 |
|---|---|---|
| 硬解开启但 CPU 仍然很高 | 帧被下载或软件转换 | 查是否用了 `ImageReader`、`glReadPixels`、CPU swscale、UIImage 转换 |
| 画面绿/紫/偏色 | YUV 格式或颜色矩阵错 | 查 NV12/NV21/I420、BT.601/BT.709、Full/Limited |
| 画面斜、撕裂、花屏 | stride/pixelStride 错 | 按 plane 的 rowStride/pixelStride 拷贝，不能按 width |
| 播放卡住 | output buffer 没释放 | Android 查 `releaseOutputBuffer`，iOS 查 callback 里 buffer 生命周期 |
| 首帧黑屏 | 参数集缺失 | H.264 查 SPS/PPS，H.265 查 VPS/SPS/PPS，确认 AVCC/Annex-B 转换 |
| 延迟越来越大 | 渲染消费慢 | 丢过期帧、限制队列长度、以音频或系统时钟追赶 |

---

## 八、自检题

1. Android `MediaCodec` 输出到 `Surface` 和输出到 `ByteBuffer/Image` 有什么本质区别？
2. 为什么 `releaseOutputBuffer(index, true)` 会影响渲染和解码器继续工作？
3. `SurfaceTexture.updateTexImage()` 做了什么？为什么要读取 transform matrix？
4. iOS 的 `CVPixelBuffer` 和 `IOSurface` 是什么关系？
5. 为什么把 `CVPixelBuffer` 转成 `UIImage` 不适合实时播放链路？
6. 硬解码已经开启，为什么 CPU 还是很高？列 3 个可能原因。
7. 如果要在硬解后做美颜/滤镜，应该优先 CPU 处理还是 GPU shader/Metal compute？为什么？
8. 如何向面试官解释“零拷贝不是完全没有内存移动，而是不发生 CPU 可见的大帧搬运”？

