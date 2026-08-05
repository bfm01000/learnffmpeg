# 零拷贝与图形缓冲学习索引

> 定位：跨平台零拷贝、CPU/GPU 内存布局、图形缓冲、同步与渲染架构深水区。平台 API 生命周期分别放在 Android/iOS 目录；这里解释“为什么这样设计、哪里会发生隐式拷贝、如何验证”。

---

## 一、面试复习路径

| 顺序 | 文件 | 面试抓手 |
|---|---|---|
| 1 | [CPU_GPU读写图像特性.md](./CPU_GPU读写图像特性.md) | CPU/GPU 访问模式、linear/tiled、stride、plane |
| 2 | [Android_4K_Live_知识点深度复习.md](./Android_4K_Live_知识点深度复习.md) | 4K 推流里的零拷贝、`glReadPixels`、AHardwareBuffer、JNI 取舍 |
| 3 | [AHardwareBuffer_从浅入深完全解析.md](./AHardwareBuffer_从浅入深完全解析.md) | AHardwareBuffer、USAGE、dma-buf、CPU_READ 陷阱 |
| 4 | [Android图形渲染.md](./Android图形渲染.md) | Surface、SurfaceTexture、OES Texture、BufferQueue、SDK 渲染架构 |
| 5 | [Android硬件编解码.md](./Android硬件编解码.md) | 硬解 AVFrame、平台硬件帧差异、FFmpeg 封装边界 |
| 6 | [GPU同步.md](./GPU同步.md) | `glFinish`、fence、跨上下文同步、为什么无 CPU 拷贝仍然慢 |
| 7 | [oryol简介.md](./oryol简介.md) | RHI 架构、跨平台抽象和击穿平台黑盒的取舍 |

---

## 二、知识主线

```text
图像数据格式
  -> CPU/GPU 访问模式差异
  -> linear / tiled / stride / plane
  -> Surface / GraphicBuffer / AHardwareBuffer / IOSurface
  -> EGLImage / OES / CVMetalTextureCache
  -> fence / glFinish / glWaitSync
  -> 采集 -> 滤镜 -> 编码 -> 渲染的零拷贝链路
```

---

## 三、与其他目录的职责边界

| 问题 | 权威位置 |
|---|---|
| Android MediaCodec 生命周期 | [../Android/02-MediaCodec硬编码实战.md](../Android/02-MediaCodec硬编码实战.md)、[../Android/03-MediaCodec硬解码实战.md](../Android/03-MediaCodec硬解码实战.md) |
| iOS VideoToolbox / Metal 生命周期 | [../iOS/05-VideoToolbox硬编码实战.md](../iOS/05-VideoToolbox硬编码实战.md)、[../iOS/04-Metal渲染与零拷贝详解.md](../iOS/04-Metal渲染与零拷贝详解.md) |
| FFmpeg 硬件帧通用模型 | [../ffmpeg/07-硬件编解码.md](../ffmpeg/07-硬件编解码.md) |
| Android/iOS 解码到渲染横向对比 | [../ffmpeg/25-硬解码到渲染流程-MediaCodec-VideoToolbox.md](../ffmpeg/25-硬解码到渲染流程-MediaCodec-VideoToolbox.md) |

---

## 四、整理策略

本目录有较多从项目经验沉淀下来的深讲，不强行合并进 Android/iOS 平台目录。后续整理时会统一术语和交叉索引：`零拷贝` 优先解释为“不发生 CPU 可见的大帧搬运”，必要时区分 zero-copy、zero CPU copy、near-zero-copy。