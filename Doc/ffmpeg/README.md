# FFmpeg 学习笔记

> **跨机器 / 跨会话回来时，先读 [99-学习进度.md](./99-学习进度.md)**——里面有一句话恢复指令和当前停在哪里。
> 这是从 16 份零散笔记重组后的索引版本。
> 入口在 [00-FFmpeg全景导读.md](./00-FFmpeg全景导读.md)——**强烈建议先读它建立地图感**，再按需深入某个主题。
> 原 16 份文档归档在 [_archive/](./_archive/)，留作回查。

---

## 推荐阅读顺序

```
00 全景导读 (先读两遍,建立地图感)
   |
   v
01 数据结构与生命周期  <- ffmpeg 代码的基础
   |
   v
02 像素格式与内存布局  <- 视频"看得对"的基础
   |
   v
03 SwsContext         <- 视频"加工车间"
   |
   v
04 音频 PCM 与重采样   <- 音频"听得对"的基础
   |
   v
05 H.264 / MP4 / NALU <- 编码和容器的关系
   |
   v
06 编码参数与码控      <- 怎么调编码效果
   |
   v
07 硬件编解码         <- 进阶,有 GPU 才学
   |
   v
10 移动端硬件编解码    <- 07 的移动端续篇 (Android/iOS),做手机/WebRTC 才学
   |
   v
08 网络协议与流媒体    <- 直播 / RTC 的基础
```

> 09 是面试题集与自检（贯穿全部主题），10 是 07 的移动端续篇——两者不在主线顺序里，按需读。

学习方法上：按导读第 12 节"当前阶段定位"的标尺给自己定位。先把已经懂的章节快速过一遍把零散点串成地图，再针对 ⚠️/❌ 的部分集中突破。

---

## 索引

| 文件 | 一句话定位 | 对应导读章节 |
|---|---|---|
| [00-FFmpeg全景导读.md](./00-FFmpeg全景导读.md) | 全景地图 + 阶段定位 + 学习路径 | 全部 |
| [01-数据结构与生命周期.md](./01-数据结构与生命周期.md) | AVPacket / AVFrame / AVCodec / AVCodecContext + alloc-ref-unref-free-uninit-close 命名约定 + CMake 接入 | 3.5 / 6.1 / 6.3 |
| [02-像素格式与内存布局.md](./02-像素格式与内存布局.md) | YUV / RGB / Planar / Packed / Semi-Planar / I420 / NV12 / NV21 / linesize / BT.601 vs 709 / Full vs Limited Range | 3.3 / 6.4 |
| [03-SwsContext-图像缩放与格式转换.md](./03-SwsContext-图像缩放与格式转换.md) | sws_getContext / sws_scale 用法 + linesize 配合 + SwsContext 复用 | 3.6 |
| [04-音频PCM-采样-重采样.md](./04-音频PCM-采样-重采样.md) | PCM 五参数 / 采样率 / 位深 / 声道布局 / SwrContext / 同步 / Jitter Buffer / AEC / AAC FIFO | 3.3 / 3.4 / 6.4 |
| [05-H264-MP4-NALU.md](./05-H264-MP4-NALU.md) | 编码 vs 容器 / NALU / AVCC vs Annex-B / I/P/B / GOP / IDR / SPS-PPS 补齐 / PTS-DTS / AAC ADTS | 3.1 / 3.2 / 6.5 |
| [06-编码参数与码控.md](./06-编码参数与码控.md) | H.264 Profile / Preset / Tune / CRF / CBR / VBR / 低延迟参数组合 | 3.1 |
| [07-硬件编解码.md](./07-硬件编解码.md) | NVENC / VideoToolbox / QSV / 硬件帧 vs 软件帧 / av_hwframe_transfer_data / 硬件滤镜 | 3.6 |
| [08-网络协议与流媒体.md](./08-网络协议与流媒体.md) | TCP vs UDP / 队头阻塞 / HTTPS / QUIC / RTMP / HLS / HTTP-FLV / WebRTC 实时栈（RTP/RTCP/ICE/SDP/SRTP/GCC/SFU）/ 协议伪装 | 3.7 |
| [09-面试题集与自检.md](./09-面试题集与自检.md) | 9 主题 × 三档 60+ 面试题 + 答题模板 + 系统设计题 + 自评打分表 + 级别判断（贯穿全部主题） | 全部 |
| [10-移动端硬件编解码.md](./10-移动端硬件编解码.md) | Android MediaCodec（缓冲区队列 / CSD / 颜色格式坑 / Surface 零拷贝）/ iOS VideoToolbox（实时编码 / CVPixelBuffer / AVCC↔Annex-B）/ FFmpeg 两端支持边界 / WebRTC 衔接 | 6.5 / 硬件 |

---

## 快速查找：按问题找文档

### "为什么我的画面是绿屏 / 花屏 / 红蓝反色？"

→ [02-像素格式与内存布局.md](./02-像素格式与内存布局.md)，重点看第 3 节"命名大乱斗"和第 7 节"常见 bug"。

### "我直接读 .mp4 文件按 00 00 01 扫 NALU，全是乱的？"

→ [05-H264-MP4-NALU.md](./05-H264-MP4-NALU.md)，第 5 节"为什么直接解析 MP4 字节会出错"。

### "解码 AAC 后送 SDL 播放全是杂音？"

→ [04-音频PCM-采样-重采样.md](./04-音频PCM-采样-重采样.md)，第 7 节"平台与库的格式偏好"。

### "音频解码 receive_frame 一次只读到第一帧，后面的丢了？"

→ [01-数据结构与生命周期.md](./01-数据结构与生命周期.md)，第 6.2 节。

### "数据从 GPU 解码出来送 sws_scale 崩溃 / 极慢？"

→ [07-硬件编解码.md](./07-硬件编解码.md)，第 5 节"sws_scale 为什么不能处理硬件帧"。

### "流媒体协议太多记不清？RTP / RTSP / RTMP / HLS / WebRTC 啥关系？"

→ [08-网络协议与流媒体.md](./08-网络协议与流媒体.md) §6.0 协议分层全景 + §6.1 主对比表（一张图看清所有协议的层级与差异）；RTSP 单独看 §6.7，WebRTC 内部栈看 §十。

### "为什么直播用 FLV / TS 不用普通 MP4？几种封装格式怎么选？"

→ [05-H264-MP4-NALU.md](./05-H264-MP4-NALU.md) §5.5 主流封装格式横向对比（MP4 的 moov / faststart / fMP4、FLV Tag 流、TS 188 字节包）。

### "讲讲 H.264 怎么把视频压缩这么小的？"

→ [05-H264-MP4-NALU.md](./05-H264-MP4-NALU.md) §7.4 编码器内部（消除四种冗余：预测 + 变换量化 + 熵编码 + 环路滤波）。

### "手机上怎么硬编硬解？Android 解码花屏 / iOS 推流只有声音没画面？"

→ [10-移动端硬件编解码.md](./10-移动端硬件编解码.md)：Android 花屏看第 3.4 节（stride / 颜色格式），iOS 推流无画面看第 4.3 节（AVCC→Annex-B + 补 SPS/PPS）。

### "FFmpeg 在 Android 上能硬件编码吗？"

→ [10-移动端硬件编解码.md](./10-移动端硬件编解码.md) 第五章：不能，没有 MediaCodec 编码 wrapper，得直接调系统 API。

### "移动端零拷贝怎么做？iOS 和 Android 有什么区别？"

→ [10-移动端硬件编解码.md](./10-移动端硬件编解码.md) 第四点五节：底层原理（UMA 下拷贝为何仍慢）、采集→编码 / 解码→渲染两条不下 CPU 的链路、Android dma-buf/Surface vs iOS IOSurface/CVPixelBuffer、踩坑表 + 面试问答（高频）。

### "桌面端（Linux/Windows/macOS）硬件编解码和零拷贝怎么做？"

→ [07-硬件编解码.md](./07-硬件编解码.md) §2.5（三大 OS 的 API 地图：VAAPI / D3D11VA / VideoToolbox）+ §五点五（桌面零拷贝与渲染互操作：CUDA-GL / VAAPI-dmabuf / D3D11 / Metal，独显的 PCIe 回读为何更贵）。

### "直播延迟为什么这么高？怎么降？"

→ [08-网络协议与流媒体.md](./08-网络协议与流媒体.md)，第 6 节"四大流媒体协议对比"+ [06-编码参数与码控.md](./06-编码参数与码控.md) 第 5 节"zerolatency"。

### "为什么直播切入新观众有时黑屏？"

→ [05-H264-MP4-NALU.md](./05-H264-MP4-NALU.md)，第 6 节"SPS / PPS 补齐"。

### "音视频不同步怎么排查？"

→ [04-音频PCM-采样-重采样.md](./04-音频PCM-采样-重采样.md)，第 10 节"时间戳与音视频同步" + 第 15 节"故障速查表"。

### "linesize 为什么不等于 width × 字节数？"

→ [02-像素格式与内存布局.md](./02-像素格式与内存布局.md) 第 4 节 + [01-数据结构与生命周期.md](./01-数据结构与生命周期.md) 第 3.3 节。

### "alloc / ref / unref / free / uninit / close 这堆 API 怎么区分？"

→ [01-数据结构与生命周期.md](./01-数据结构与生命周期.md) 第 5 节。

---

## 关于归档

原 16 份笔记完整保留在 [_archive/](./_archive/) 里。重组的核心动作：

- **删除 1 份完全重复**：`mp4_h264_mock_interview.md` 和 `h264_mp4_模拟面试.md` 内容逐字相同，归档但只引用一份内容
- **合并去重 4 处主题分散**：AVPacket / AVFrame、Planar/Packed、音频 PCM、H.264/MP4 在原文档里多份散落
- **CMake 笔记拆分**：原 `ffmpeg_cmake_questions.md` 里"是 CMake"的部分放进 01 的第 8 节，"是 AVPacket 行为"的部分放进 01 的第 7 节
- **硬件帧讨论归位**：原 `swscontext_lecture.md` 开头的硬件帧部分挪到 07
- **基础概念归位**：原 `音视频开发常见问题与解析.md` 里 YUV / Color Range 进 02，I/P/B / GOP / PTS-DTS / NALU / ADTS 进 05，流媒体协议进 08

如果你想看某一份原文档的完整原貌：

```bash
ls Doc/ffmpeg/_archive/
```

---

## 下一步建议

读完导读建立地图感之后，**不要继续在 ffmpeg 笔记里打转**。

按导读第 13 节，下一步是**动手写一段最简流水线代码**：

```
demux MP4 → decode H.264 → swscale 转 RGB → SDL 显示
```

这段代码不长，但写完你对 AVPacket / AVFrame / time_base / 引用计数 / Planar/Packed 的理解会**从"读过"变成"骨头里的"**。比读十遍 MP4 box 协议管用十倍。
