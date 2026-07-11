# iOS 音视频硬件编解码 Demo 合集

本目录包含完整的、可直接集成到 Xcode 项目的 Objective-C demo 代码。
每个 demo 都是独立的 `.h` + `.m` 文件对，覆盖 iOS 音视频开发的核心环节。

## Demo 列表

| # | Demo | 文件 | 覆盖知识点 |
|---|------|------|-----------|
| 1 | 视频采集 | `VideoCaptureDemo.h/.m` | AVCaptureSession 完整搭建、NV12 输出、前后摄切换、权限 |
| 2 | H.264 硬编码 | `VTH264Encoder.h/.m` | VTCompressionSession、实时编码、动态码率、强制 IDR |
| 3 | H.264 硬解码 | `VTH264Decoder.h/.m` | VTDecompressionSession、Annex-B→AVCC、SPS/PPS 提取 |
| 4 | Metal YUV 渲染 | `MetalYUVRenderer.h/.m` | CVMetalTextureCache 零拷贝、NV12→RGB shader、MTKView |
| 5 | 格式转换 | `H264FormatConverter.h/.m` | AVCC↔Annex-B 双向转换、SPS/PPS 注入、HEVC VPS |
| 6 | 音频管理 | `AudioSessionManager.h/.m` | AudioSession 配置、中断处理、路由变化、前后台切换 |
| 7 | 端到端推流 | `RTMPBroadcaster.h/.m` | 采集→编码→推流 完整管线、线程模型、Metal 预览 |

## 使用方式

1. 将 `.h` 和 `.m` 文件拖入你的 Xcode 项目
2. 确保项目链接了以下框架：
   - `AVFoundation.framework`
   - `VideoToolbox.framework`
   - `CoreMedia.framework`
   - `CoreVideo.framework`
   - `Metal.framework`
   - `MetalKit.framework`
   - `AudioToolbox.framework`
3. 在需要的地方 `#import` 对应的头文件

## 学习路径

建议按以下顺序学习：

```
1. VideoCaptureDemo     → 理解采集：拿到 NV12 的 CVPixelBuffer
2. MetalYUVRenderer     → 理解渲染：CVPixelBuffer → Metal 纹理 → 屏幕
3. VTH264Encoder        → 理解编码：CVPixelBuffer → H.264 比特流
4. H264FormatConverter  → 理解格式：AVCC ↔ Annex-B
5. VTH264Decoder        → 理解解码：H.264 比特流 → CVPixelBuffer
6. AudioSessionManager  → 理解音频策略：中断、路由、前后台
7. RTMPBroadcaster      → 贯穿全景：把前面 6 个串起来
```

## 关联文档

- [[../00-iOS音视频开发全景导读]] — 全景索引
- [[../04-Metal渲染与零拷贝详解]] — Metal 渲染深入
- [[../05-VideoToolbox硬编码实战]] — 编码深入
- [[../06-VideoToolbox硬解码实战]] — 解码深入
- [[../07-AVCC与Annex-B转换实战]] — 格式转换深入
- [[../08-端到端采集编码推流管线]] — 端到端深入
- [[../09-AudioSession与音频策略详解]] — AudioSession 深入
- [[../99-iOS音视频面试题全集]] — 面试题全集
