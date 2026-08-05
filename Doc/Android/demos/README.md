# Android 音视频硬件编解码 Demo 合集

## 0. 本篇定位

- 面试复习：把这里当成 Android 平台 API 的最小代码索引，重点看采集、编码、解码、OpenGL 渲染四类 demo 如何对应真实工程模块。
- 深入学习：每个独立 `.java` 文件只保留最小可读实现；完整链路、线程模型、异常处理和性能取舍回到上级专题文档。
- 使用边界：当前目录只索引实际存在的示例文件；端到端推流、音频采集播放等示例以内嵌代码形式保留在对应章节。

## Demo 列表

| # | Demo | 当前文件 | 覆盖知识点 | 深入文档 |
|---|---|---|---|---|
| 1 | H.264 硬编码 | `MediaCodecEncoder.java` | MediaCodec 编码、ByteBuffer 模式、实时属性、输出格式变化 | [02-MediaCodec硬编码实战](../02-MediaCodec硬编码实战.md) |
| 2 | H.264 硬解码 | `MediaCodecDecoder.java` | MediaCodec 解码、Surface 输出、csd-0/csd-1、EOS | [03-MediaCodec硬解码实战](../03-MediaCodec硬解码实战.md) |
| 3 | OpenGL YUV 渲染 | `GLYUVRenderer.java` | YUV 到 RGB shader、纹理上传、基础 GL 渲染 | [04-OpenGLES渲染与Surface详解](../04-OpenGLES渲染与Surface详解.md) |

## 章节内嵌示例

| 主题 | 所在文档 | 复习重点 |
|---|---|---|
| Camera2 采集 | [01-Camera2采集详解](../01-Camera2采集详解.md) | `ImageReader`、`YUV_420_888`、`rowStride/pixelStride`、采集回调线程 |
| Android 音频 | [05-AudioTrack与AudioRecord详解](../05-AudioTrack与AudioRecord详解.md) | `AudioRecord`、`AudioTrack`、AAudio、AEC/NS/AGC、underrun/overrun |
| 端到端推流 | [06-端到端采集编码推流管线](../06-端到端采集编码推流管线.md) | `Camera2 -> OES/FBO -> MediaCodec -> RTMP`、时间戳、线程和背压 |

## 使用建议

1. 面试前先读上级专题的“面试速记”和“常见坑”，再回到 demo 看代码入口。
2. 代码验证时优先关注输入输出格式、生命周期、线程和错误恢复，不要只看 API 调用顺序。
3. 如果后续补齐独立 `Camera2CaptureManager.java`、`AudioCapture.java`、`LiveStreamer.java`，需要同步更新本索引和 `Doc/Android/README.md`。

## 关联文档

- [Android 音视频学习索引](../README.md)
- [Android 音视频开发全景导读](../00-Android音视频开发全景导读.md)
- [Android 音视频面试题全集](../99-Android音视频面试题全集.md)
- [FFmpeg 硬解到渲染流程](../../ffmpeg/25-硬解码到渲染流程-MediaCodec-VideoToolbox.md)