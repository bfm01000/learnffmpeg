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
11 H.264 与 H.265 详解 <- 编解码专题(面试导向),想吃透编码原理就读
   |
   v
07 硬件编解码         <- 进阶,有 GPU 才学 (通用底座 + 平台地图)
   |
   v
10 移动端硬件编解码    <- 07 的移动端续篇 (Android/iOS 总览 + 零拷贝),做手机/WebRTC 才学
   |
   v
13/14/15 平台专题      <- 按需深挖单个平台 (NVIDIA / Android / iOS),面试前重点看
   |
   v
16 硬件编解码高级专题   <- 13/14/15 横向进阶 (LTR/SVC/VMAF/延迟/容量/DRM),冲高级岗必读
   |
   v
08 网络协议与流媒体    <- 直播 / RTC 的基础
   |
   v
12 RTMP 推流详解       <- 08 推流那一格的深挖,做直播/推流才学
   |
   v
19 HLS 详解             <- 08 分发那一格的深挖,做直播/CDN分发才学
```

> 09 是面试题集与自检（贯穿全部主题）；10 是 07 的移动端总览，13/14/15 分别是 NVIDIA / Android / iOS 的专题深挖，16 是它们的跨平台高级进阶——都不在主线顺序里，按需读。

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
| [09-题集与自检.md](./09-题集与自检.md) | 9 主题 × 三档 68+ 面试题（§4 音频 15 题）+ 答题模板 + 系统设计题 + 自评打分表 + 级别判断（贯穿全部主题） | 全部 |
| [10-移动端硬件编解码.md](./10-移动端硬件编解码.md) | Android MediaCodec（缓冲区队列 / CSD / 颜色格式坑 / Surface 零拷贝）/ iOS VideoToolbox（实时编码 / CVPixelBuffer / AVCC↔Annex-B）/ FFmpeg 两端支持边界 / WebRTC 衔接 | 6.5 / 硬件 |
| [11-H264与H265详解.md](./11-H264与H265详解.md) | 编解码专题（面试导向）：压缩原理（四种冗余）/ H.264 宏块/帧类型/CAVLC-CABAC / H.265 CTU/SAO/WPP/VPS / 全面对比 / 使用场景与授权 / 坑与误区 / 面试速记 + 常考题 | 3.1 |
| [12-RTMP推流详解.md](./12-RTMP推流详解.md) | RTMP 推流专题（08 推流深挖）：推流全链路 / 握手 C0-C1-C2 / Chunk 分块 / AMF 命令 connect-createStream-publish / 和 FLV Tag 关系 / ffmpeg-C API 推流实战 / 坑与误区 / 面试常考题 | 3.7 |
| [19-HLS详解.md](./19-HLS详解.md) | HLS 分发专题（08 分发深挖）：m3u8 播放列表 / TS 切片结构 / 自包含切片（核心概念+面试高频）/ FFmpeg 切 HLS / LL-HLS 低延迟 / CMAF fMP4 / 多码率 ABR / 坑与排查 / 面试常考题 | 3.7 |
| [13-NVIDIA硬件编解码.md](./13-NVIDIA硬件编解码.md) | NVIDIA 专题（07 深挖，面试导向）：NVENC/NVDEC/CUVID 本质 / 代际能力（AV1 分界、并发 session 限制）/ FFmpeg 集成 / P1-P7 preset 与码控 / CUDA interop / DeepStream / 横向对比 + 面试问答 | 3.6 / 硬件 |
| [14-Android硬件编解码.md](./14-Android硬件编解码.md) | Android 专题（10 深挖，面试导向）：MediaCodec/AMediaCodec / OMX vs Codec2 / 硬软 codec 区分 / 缓冲区队列 + 生命周期状态机 / CSD / 颜色格式坑 / Surface 零拷贝 / 码控 / FFmpeg 边界 + 面试问答 | 6.5 / 硬件 |
| [15-iOS硬件编解码.md](./15-iOS硬件编解码.md) | iOS/macOS 专题（10 深挖，面试导向）：VideoToolbox 编解码生命周期 / 核心对象 / 实时编码属性 / AVCC↔Annex-B 字节级 / CVPixelBuffer-Metal 零拷贝 / 软硬选择 / FFmpeg 边界 / WebRTC 衔接 + 面试问答 | 6.5 / 硬件 |
| [16-硬件编解码高级专题.md](./16-硬件编解码高级专题.md) | 跨平台高级专题（13/14/15 横向进阶，**冲高级岗**）：LTR 长期参考帧 / Intra-Refresh-GDR / 时域分层 SVC + Simulcast / VMAF-BD-rate 质量评估 / glass-to-glass 延迟拆解 / 吞吐与容量规划 / 安全解码 DRM-Widevine L1 / 错误恢复 + 面试问答 | 硬件 |

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

### "RTMP 推流怎么做？握手/连接流程？和 FLV 什么关系？推流黑屏花屏？"

→ [12-RTMP推流详解.md](./12-RTMP推流详解.md)：推流全链路、握手 C0/C1/C2、Chunk 分块、connect/createStream/publish、RTMP↔FLV Tag 关系、ffmpeg C API 推流；**花屏/黑屏/卡顿三分法排查（§11.1~§11.3）** + 坑表 + 面试题。

### "为什么直播用 FLV / TS 不用普通 MP4？几种封装格式怎么选？"

→ [05-H264-MP4-NALU.md](./05-H264-MP4-NALU.md) §5.5 主流封装格式横向对比（MP4 的 moov / faststart / fMP4、FLV Tag 流、TS 188 字节包）。

### "讲讲 H.264 怎么把视频压缩这么小的？H.264 和 H.265 区别？"

→ [11-H264与H265详解.md](./11-H264与H265详解.md)：编解码专题，压缩原理（四种冗余）、H.264 vs H.265 全面对比、使用场景与授权、坑与误区、面试速记 + 常考题。（编码原理速览也在 [05](./05-H264-MP4-NALU.md) §7.4）

### "手机上怎么硬编硬解？Android 解码花屏 / iOS 推流只有声音没画面？"

→ [10-移动端硬件编解码.md](./10-移动端硬件编解码.md)：Android 花屏看第 3.4 节（stride / 颜色格式），iOS 推流无画面看第 4.3 节（AVCC→Annex-B + 补 SPS/PPS）。

### "FFmpeg 在 Android 上能硬件编码吗？"

→ [10-移动端硬件编解码.md](./10-移动端硬件编解码.md) 第五章：FFmpeg 6.0+ 虽有 `h264_mediacodec` 编码器但能力受限，生产里 Android 硬编基本还是直调系统 API（MediaCodec/AMediaCodec）。

### "移动端零拷贝怎么做？iOS 和 Android 有什么区别？"

→ [10-移动端硬件编解码.md](./10-移动端硬件编解码.md) 第四点五节：底层原理（UMA 下拷贝为何仍慢）、采集→编码 / 解码→渲染两条不下 CPU 的链路、Android dma-buf/Surface vs iOS IOSurface/CVPixelBuffer、踩坑表 + 面试问答（高频）。

### "桌面端（Linux/Windows/macOS）硬件编解码和零拷贝怎么做？"

→ [07-硬件编解码.md](./07-硬件编解码.md) §2.5（三大 OS 的 API 地图：VAAPI / D3D11VA / VideoToolbox）+ §五点五（桌面零拷贝与渲染互操作：CUDA-GL / VAAPI-dmabuf / D3D11 / Metal，独显的 PCIe 回读为何更贵）。

### "想系统搞懂某一个平台的硬编硬解（面试要问到 NVIDIA / Android / iOS）？"

→ 单平台深挖专题：[13-NVIDIA硬件编解码.md](./13-NVIDIA硬件编解码.md)（NVENC/NVDEC、代际能力、并发 session、CUDA interop、DeepStream）、[14-Android硬件编解码.md](./14-Android硬件编解码.md)（MediaCodec、OMX vs Codec2、缓冲区状态机、颜色格式坑、Surface 零拷贝、FFmpeg 不能硬编）、[15-iOS硬件编解码.md](./15-iOS硬件编解码.md)（VideoToolbox 生命周期、AVCC↔Annex-B、CVPixelBuffer-Metal、WebRTC 衔接）。每篇都带高频面试问答。

### "硬件编解码的高级面试题（弱网恢复 / 多人会议编码 / 怎么证明画质 / 一台机器跑多少路 / 付费内容为啥不能截图）？"

→ [16-硬件编解码高级专题.md](./16-硬件编解码高级专题.md)：LTR 长期参考帧、Intra-Refresh/GDR、时域分层 SVC + Simulcast、VMAF/BD-rate 质量评估、glass-to-glass 延迟拆解、吞吐与容量规划、安全解码（Widevine L1/L3）、错误恢复——13/14/15 的跨平台进阶，冲高级岗必读。

### "直播延迟为什么这么高？怎么降？"

→ [08-网络协议与流媒体.md](./08-网络协议与流媒体.md)，第 6 节"四大流媒体协议对比"+ [06-编码参数与码控.md](./06-编码参数与码控.md) 第 5 节"zerolatency"。

### "HLS 切片怎么切？第二个分片为什么也要带 SPS/PPS？怎么降延迟？"

→ [19-HLS详解.md](./19-HLS详解.md)：HLS 全链路（m3u8 + TS + 自包含 + LL-HLS + ABR），面试高频 §二 Q3，自包含核心概念 §3.4，延迟优化 §五。

### "为什么直播切入新观众有时黑屏？"

→ [05-H264-MP4-NALU.md](./05-H264-MP4-NALU.md)，第 6 节"SPS / PPS 补齐" + [19-HLS详解.md](./19-HLS详解.md) §3.4（HLS 自包含切片专题）。

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
