# iOS 音视频学习索引

> 定位：iOS / macOS 平台音视频实战专题。通用编解码、时间戳、H.264/MP4/RTMP 等原理统一回链到 [../ffmpeg/README.md](../ffmpeg/README.md)。

---

## 一、面试复习路径

| 顺序 | 文件 | 面试抓手 |
|---|---|---|
| 1 | [00-iOS音视频开发全景导读.md](./00-iOS音视频开发全景导读.md) | iOS 媒体栈三层架构、AVFoundation、VideoToolbox、Metal、AudioUnit |
| 2 | [01-AVFoundation采集详解.md](./01-AVFoundation采集详解.md) | `AVCaptureSession`、`CMSampleBuffer`、`CVPixelBuffer`、采集线程 |
| 3 | [05-VideoToolbox硬编码实战.md](./05-VideoToolbox硬编码实战.md) | `VTCompressionSession`、实时编码属性、SPS/PPS、码率控制 |
| 4 | [06-VideoToolbox硬解码实战.md](./06-VideoToolbox硬解码实战.md) | `VTDecompressionSession`、format description、解码回调、恢复策略 |
| 5 | [07-AVCC与Annex-B转换实战.md](./07-AVCC与Annex-B转换实战.md) | iOS 推流必考的码流格式转换 |
| 6 | [04-Metal渲染与零拷贝详解.md](./04-Metal渲染与零拷贝详解.md) | `CVMetalTextureCache`、NV12 到 Metal texture、YUV/RGB shader |
| 7 | [10-iOS零拷贝深入详解.md](./10-iOS零拷贝深入详解.md)、[11-IOSurface深入详解.md](./11-IOSurface深入详解.md) | IOSurface、CVPixelBufferPool、跨框架共享、零拷贝验证 |
| 8 | [99-iOS音视频面试题全集.md](./99-iOS音视频面试题全集.md) | 平台题集中刷 |

---

## 二、详细学习路径

```text
AVFoundation 采集
  -> CMSampleBuffer / CVPixelBuffer
  -> VideoToolbox 编码 / 解码
  -> AVCC / Annex-B 转换
  -> Metal / CVMetalTextureCache 渲染
  -> AudioUnit / AudioSession
  -> RTMP 推流管线
  -> IOSurface 零拷贝深入
```

---

## 三、与其他目录的职责边界

| 问题 | 权威位置 |
|---|---|
| H.264/HEVC 码流通用原理 | [../ffmpeg/05-H264-MP4-NALU.md](../ffmpeg/05-H264-MP4-NALU.md) |
| 码控 CRF/CBR/VBR/GOP/低延迟 | [../ffmpeg/06-编码参数与码控.md](../ffmpeg/06-编码参数与码控.md) |
| iOS 与 Android 硬解到渲染横向对比 | [../ffmpeg/25-硬解码到渲染流程-MediaCodec-VideoToolbox.md](../ffmpeg/25-硬解码到渲染流程-MediaCodec-VideoToolbox.md) |
| CPU/GPU 内存布局和跨平台零拷贝 | [../零拷贝/README.md](../零拷贝/README.md) |
| RTMP 推流协议和 FLV Tag | [../ffmpeg/12-RTMP推流详解.md](../ffmpeg/12-RTMP推流详解.md) |

---

## 四、整理建议

本目录保留 iOS 平台 API 和工程实战细节；如果某个概念是跨平台共性，例如 SPS/PPS、PTS/DTS、Annex-B/AVCC、音视频同步，只在本文给平台差异和坑，详细原理回链到 `ffmpeg/`。