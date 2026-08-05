# 音视频学习文档总索引

> 目标：把本项目中的 FFmpeg、播放器、编解码、流媒体、Android/iOS 音视频、零拷贝和 WebRTC 学习资料串成一套长期可维护的知识体系。
>
> 当前重构进度见根目录 [docs_reorganization_progress.md](../docs_reorganization_progress.md)。只要该进度文件中仍有 `未检查`、`待合并` 或 `整理中`，就说明全局整理还在继续。

---

## 一、推荐学习路线

```text
FFmpeg 全景与基础 API
  -> AVPacket / AVFrame / 时间戳 / 内存管理
  -> 解码 / 编码 / 封装 / 转码实战
  -> 像素格式 / 音频 PCM / swscale / swresample
  -> H.264 / H.265 / AAC / MP4 / FLV / TS
  -> 音视频同步 / Seek / Flush / EAGAIN
  -> RTMP / HLS / RTP / WebRTC
  -> Android / iOS 硬编硬解
  -> 零拷贝 / Surface / IOSurface / AHardwareBuffer
  -> C++ 工程底座：并发、RAII、内存池、FFmpeg 资源封装
  -> 播放器、转码器、WebRTC 项目实战
  -> 面试题库与项目讲述
```

---

## 二、核心文档入口

| 模块 | 入口 | 定位 |
|---|---|---|
| FFmpeg 核心 | [ffmpeg/README.md](./ffmpeg/README.md) | API、编解码、封装、同步、Seek、流媒体、硬件编解码、面试题主线 |
| C++ 工程底座 | [cpp/00-导读与索引.md](./cpp/00-导读与索引.md) | 并发、RAII、移动语义、内存池、无锁队列、FFmpeg C API 封装 |
| Android 音视频 | [Android/README.md](./Android/README.md) | Camera2、MediaCodec、OpenGLES、AudioTrack、端到端推流 |
| iOS 音视频 | [iOS/README.md](./iOS/README.md) | AVFoundation、VideoToolbox、Metal、AudioUnit、IOSurface |
| 直播协议 | [直播/README.md](./直播/README.md) | RTMP、HTTP-FLV、WebRTC 直播架构的业务化深讲 |
| 网络协议 | [网络协议/README.md](./网络协议/README.md) | TCP、RTMP、RTP/WebRTC、弱网传输、抓包排障 |
| 零拷贝 | [零拷贝/README.md](./零拷贝/README.md) | Android 图形缓冲、AHardwareBuffer、CPU/GPU 同步、4K 推流经验 |
| 自动剪辑 | [自动剪辑/seek优化.md](./自动剪辑/seek优化.md) | 帧索引、Seek 精度、剪辑时间模型 |

---

## 三、按面试场景快速定位

| 面试问题 | 先看 | 深入补充 |
|---|---|---|
| FFmpeg 播放/转码主链路怎么讲 | [ffmpeg/00-FFmpeg全景导读.md](./ffmpeg/00-FFmpeg全景导读.md) | `project/8_Simplest_Player`、`project/1_transcoder` |
| `AVPacket` / `AVFrame` / 引用计数 | [ffmpeg/01-数据结构与生命周期.md](./ffmpeg/01-数据结构与生命周期.md) | [cpp/23-C与C++互操作及FFmpeg资源封装.md](./cpp/23-C与C++互操作及FFmpeg资源封装.md) |
| PTS/DTS/time_base/音视频同步 | [ffmpeg/24-音视频同步详解.md](./ffmpeg/24-音视频同步详解.md) | [ffmpeg/17-FFmpeg-Seek详解.md](./ffmpeg/17-FFmpeg-Seek详解.md)、[ffmpeg/22-VFR与CFR详解.md](./ffmpeg/22-VFR与CFR详解.md) |
| H.264/H.265/SPS/PPS/Annex-B | [ffmpeg/05-H264-MP4-NALU.md](./ffmpeg/05-H264-MP4-NALU.md) | [iOS/07-AVCC与Annex-B转换实战.md](./iOS/07-AVCC与Annex-B转换实战.md) |
| Android MediaCodec | [Android/02-MediaCodec硬编码实战.md](./Android/02-MediaCodec硬编码实战.md) | [Android/03-MediaCodec硬解码实战.md](./Android/03-MediaCodec硬解码实战.md)、[ffmpeg/14-Android硬件编解码.md](./ffmpeg/14-Android硬件编解码.md) |
| iOS VideoToolbox | [iOS/05-VideoToolbox硬编码实战.md](./iOS/05-VideoToolbox硬编码实战.md) | [iOS/06-VideoToolbox硬解码实战.md](./iOS/06-VideoToolbox硬解码实战.md)、[ffmpeg/15-iOS硬件编解码.md](./ffmpeg/15-iOS硬件编解码.md) |
| 零拷贝 / Surface / IOSurface | [ffmpeg/25-硬解码到渲染流程-MediaCodec-VideoToolbox.md](./ffmpeg/25-硬解码到渲染流程-MediaCodec-VideoToolbox.md) | [零拷贝/AHardwareBuffer_从浅入深完全解析.md](./零拷贝/AHardwareBuffer_从浅入深完全解析.md)、[iOS/11-IOSurface深入详解.md](./iOS/11-IOSurface深入详解.md) |
| RTMP/HLS/RTP/WebRTC 协议选型 | [ffmpeg/08-网络协议与流媒体.md](./ffmpeg/08-网络协议与流媒体.md) | [直播/RTMP直播协议深入理解与面试指南.md](./直播/RTMP直播协议深入理解与面试指南.md)、[ffmpeg/21-RTP详解.md](./ffmpeg/21-RTP详解.md) |
| WebRTC 实时音视频 | ../project/WebRTC/00-README.md | ../project/WebRTC/11-WebRTC-QoS四驾马车-GCC-FEC-NACK-JitterBuffer.md |
| 面试题总刷 | [ffmpeg/100-核心必会问题.md](./ffmpeg/100-核心必会问题.md) | [ffmpeg/20-音视频开发面试题库-中高级.md](./ffmpeg/20-音视频开发面试题库-中高级.md)、[Android/99-Android音视频面试题全集.md](./Android/99-Android音视频面试题全集.md)、[iOS/99-iOS音视频面试题全集.md](./iOS/99-iOS音视频面试题全集.md) |

---

## 四、目录职责边界

| 目录 | 保留什么 | 不再重复什么 |
|---|---|---|
| `ffmpeg/` | 跨平台 FFmpeg/音视频通用知识和面试主线 | 不展开平台 API 的全部细节，只做对比和入口 |
| `cpp/` | 中高级 C++ 音视频工程底座：并发、资源管理、内存与流水线 | 不重复 FFmpeg API 使用流程，只承接工程化封装和性能支撑 |
| `Android/` | Android 平台实战：Camera2、MediaCodec、Surface、OpenGLES、AudioTrack | 不重复 FFmpeg 基础概念 |
| `iOS/` | iOS 平台实战：AVFoundation、VideoToolbox、Metal、AudioUnit、IOSurface | 不重复 H.264/PTS 等通用原理，只回链 |
| `直播/` | 直播协议与业务链路深讲 | 不重复 FFmpeg API 模板 |
| `网络协议/` | TCP/RTMP/RTP 等底层协议、字段和排障 | 不重复直播业务链路和平台采集编码细节 |
| `零拷贝/` | CPU/GPU/图形缓冲底层机制 | 不重复平台编码器生命周期 |
| `自动剪辑/` | 剪辑、Seek、帧索引和时间模型 | 不重复通用 Seek API 讲解 |

---

## 五、整理状态

本目录不是最终完成态。当前已完成 `Doc/ffmpeg`、Android / iOS / 零拷贝第二批整理、`Doc/cpp` 工程支撑目录，以及直播/网络协议目录的入口定位和职责边界；接下来会按 [docs_reorganization_progress.md](../docs_reorganization_progress.md) 继续处理 WebRTC、示例工程和面试材料。