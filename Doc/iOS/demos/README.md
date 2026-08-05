# iOS 音视频硬件编解码 Demo 合集

## 0. 本篇定位

- 面试复习：把这里当成 iOS 平台核心代码的最小索引，重点看 VideoToolbox、Metal、AVCC/Annex-B 和 AudioSession 的实际代码形态。
- 深入学习：独立 demo 只覆盖关键模块；采集和端到端推流的完整讲解以内嵌代码形式保留在上级专题文档。
- 使用边界：当前目录只索引实际存在的 `.h/.m` 文件，避免复习时被不存在的文件打断。

## Demo 列表

| # | Demo | 当前文件 | 覆盖知识点 | 深入文档 |
|---|---|---|---|---|
| 1 | H.264 硬编码 | `VTH264Encoder.h/.m` | `VTCompressionSession`、实时编码、动态码率、强制 IDR、SPS/PPS | [05-VideoToolbox硬编码实战](../05-VideoToolbox硬编码实战.md) |
| 2 | H.264 硬解码 | `VTH264Decoder.h/.m` | `VTDecompressionSession`、Annex-B 到 AVCC、SPS/PPS 提取、恢复策略 | [06-VideoToolbox硬解码实战](../06-VideoToolbox硬解码实战.md) |
| 3 | Metal YUV 渲染 | `MetalYUVRenderer.h/.m` | `CVMetalTextureCache`、NV12 到 RGB shader、`MTKView` | [04-Metal渲染与零拷贝详解](../04-Metal渲染与零拷贝详解.md) |
| 4 | 格式转换 | `H264FormatConverter.h/.m` | AVCC 与 Annex-B 双向转换、SPS/PPS 注入、HEVC VPS | [07-AVCC与Annex-B转换实战](../07-AVCC与Annex-B转换实战.md) |
| 5 | 音频策略 | `AudioSessionManager.h/.m` | AudioSession 配置、中断处理、路由变化、前后台切换 | [09-AudioSession与音频策略详解](../09-AudioSession与音频策略详解.md) |

## 章节内嵌示例

| 主题 | 所在文档 | 复习重点 |
|---|---|---|
| AVFoundation 采集 | [01-AVFoundation采集详解](../01-AVFoundation采集详解.md) | `AVCaptureSession`、`CMSampleBuffer`、`CVPixelBuffer`、丢帧策略 |
| AudioUnit 实时音频 | [02-AudioUnit与音频处理详解](../02-AudioUnit与音频处理详解.md) | RemoteIO、ASBD、音频回调线程、AEC 和路由 |
| 端到端推流 | [08-端到端采集编码推流管线](../08-端到端采集编码推流管线.md) | `AVFoundation -> VideoToolbox -> AVCC/Annex-B -> RTMP`、同步和异常恢复 |
| iOS 零拷贝 | [10-iOS零拷贝深入详解](../10-iOS零拷贝深入详解.md) | `CVPixelBuffer`、`IOSurface`、`CVMetalTextureCache`、零拷贝验证 |

## 使用建议

1. 面试复习先看 `99-iOS音视频面试题全集.md`，答不顺的题再回到对应 demo 和专题文档。
2. 看代码时重点追生命周期、回调线程、buffer 所有权和错误恢复，这些比 API 名称更能体现中高级经验。
3. 如果后续补齐独立 `VideoCaptureDemo.h/.m` 或 `RTMPBroadcaster.h/.m`，需要同步更新本索引和 `Doc/iOS/README.md`。

## 关联文档

- [iOS 音视频学习索引](../README.md)
- [iOS 音视频开发全景导读](../00-iOS音视频开发全景导读.md)
- [iOS 音视频面试题全集](../99-iOS音视频面试题全集.md)
- [FFmpeg 硬解到渲染流程](../../ffmpeg/25-硬解码到渲染流程-MediaCodec-VideoToolbox.md)