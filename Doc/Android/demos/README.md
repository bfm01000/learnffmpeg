# Android 音视频硬件编解码 Demo 合集

本目录包含完整的 Android 音视频开发 demo 代码（Java）。
每个 demo 是独立的 `.java` 文件，可直接集成到 Android Studio 项目。

## Demo 列表

| # | Demo | 文件 | 覆盖知识点 |
|---|------|------|-----------|
| 1 | 视频采集 | `Camera2CaptureManager.java` | Camera2 API、ImageReader、YUV_420_888、plane 拷贝 |
| 2 | H.264 硬编码 | `MediaCodecEncoder.java` | MediaCodec 编码、ByteBuffer 模式、实时属性、动态码率 |
| 3 | H.264 硬解码 | `MediaCodecDecoder.java` | MediaCodec 解码、Surface 零拷贝、csd-0/csd-1 |
| 4 | OpenGL 渲染 | `GLYUVRenderer.java` | SurfaceTexture、OES 纹理、YUV→RGB shader、EGL |
| 5 | 音频采集播放 | `AudioCapture.java` | AudioRecord、AudioTrack、AAudio、VOICE_COMMUNICATION |
| 6 | 端到端推流 | `LiveStreamer.java` | Camera2→MediaCodec→RTMP 完整管线 |

## 使用方式

1. 将 `.java` 文件复制到 Android Studio 项目的对应包路径下
2. 确保 `AndroidManifest.xml` 声明了必要权限：
   - `android.permission.CAMERA`
   - `android.permission.RECORD_AUDIO`
   - `android.permission.INTERNET`
3. minSdkVersion 建议 21+（部分功能需 26+）

## 学习路径

建议按以下顺序学习：

```
1. Camera2CaptureManager  → 理解采集：拿到 YUV_420_888 数据
2. MediaCodecEncoder      → 理解编码：YUV → H.264 Annex-B
3. MediaCodecDecoder      → 理解解码：H.264 → Surface 零拷贝输出
4. GLYUVRenderer          → 理解渲染：SurfaceTexture → OES 纹理 → 屏幕
5. AudioCapture           → 理解音频：PCM 采集和播放
6. LiveStreamer           → 贯穿全景：把前面 5 个串起来
```

## 关联文档

- [[../00-Android音视频开发全景导读]] — 全景索引
- [[../01-Camera2采集详解]] — Camera2 深入
- [[../02-MediaCodec硬编码实战]] — 编码深入
- [[../03-MediaCodec硬解码实战]] — 解码深入
- [[../04-OpenGLES渲染与Surface详解]] — 渲染深入
- [[../05-AudioTrack与AudioRecord详解]] — 音频深入
- [[../06-端到端采集编码推流管线]] — 端到端深入
- [[../99-Android音视频面试题全集]] — 面试题全集
