# Android 音视频学习索引

> 定位：Android 平台音视频实战专题。通用 FFmpeg、H.264、时间戳、音视频同步原理不在这里重复展开，统一回链到 [../ffmpeg/README.md](../ffmpeg/README.md)。

---

## 一、面试复习路径

| 顺序 | 文件 | 面试抓手 |
|---|---|---|
| 1 | [00-Android音视频开发全景导读.md](./00-Android音视频开发全景导读.md) | Android 媒体栈、Camera2、MediaCodec、OpenGLES、AudioTrack 总图 |
| 2 | [02-MediaCodec硬编码实战.md](./02-MediaCodec硬编码实战.md) | 硬编码状态机、Surface 输入、CSD、动态码率、关键帧 |
| 3 | [03-MediaCodec硬解码实战.md](./03-MediaCodec硬解码实战.md) | Surface 输出、ByteBuffer 模式、`releaseOutputBuffer`、硬解帧槽位 |
| 4 | [04-OpenGLES渲染与Surface详解.md](./04-OpenGLES渲染与Surface详解.md) | `Surface` / `SurfaceTexture` / `BufferQueue` / OES 纹理 |
| 5 | [07-Android-OES纹理深入详解.md](./07-Android-OES纹理深入详解.md) | OES 纹理限制、`updateTexImage`、EGLImage、零拷贝链路 |
| 6 | [06-端到端采集编码推流管线.md](./06-端到端采集编码推流管线.md) | Camera2 到 RTMP 的线程、时间戳、SPS/PPS、JNI 推流链路 |
| 7 | [99-Android音视频面试题全集.md](./99-Android音视频面试题全集.md) | 平台题集中刷 |

---

## 二、详细学习路径

```text
Camera2 采集
  -> ImageReader / Surface 两条通路
  -> MediaCodec 编码 / 解码
  -> SurfaceTexture / OpenGL ES 渲染
  -> AudioRecord / AudioTrack
  -> 端到端采集编码推流
  -> OES / AHardwareBuffer / BufferQueue 零拷贝深入
```

---

## 三、与其他目录的职责边界

| 问题 | 权威位置 |
|---|---|
| H.264 / SPS / PPS / Annex-B / AVCC | [../ffmpeg/05-H264-MP4-NALU.md](../ffmpeg/05-H264-MP4-NALU.md) |
| FFmpeg 硬件帧和 `sws_scale` 边界 | [../ffmpeg/07-硬件编解码.md](../ffmpeg/07-硬件编解码.md) |
| Android 与 iOS 硬解到渲染横向对比 | [../ffmpeg/25-硬解码到渲染流程-MediaCodec-VideoToolbox.md](../ffmpeg/25-硬解码到渲染流程-MediaCodec-VideoToolbox.md) |
| AHardwareBuffer / dma-buf / GPU 同步深水区 | [../零拷贝/README.md](../零拷贝/README.md) |
| RTMP/HLS/RTP/WebRTC 协议选型 | [../ffmpeg/08-网络协议与流媒体.md](../ffmpeg/08-网络协议与流媒体.md) |

---

## 四、合并说明

`Untitled` 中的学习策略问题已归档到 `_archive/merged-2026-08-04/learning-strategy-question.txt`。后续 Android 目录只保留可直接复习或深入学习的正文文档。