# 面试讲解：Native libwebrtc Sender 设计

## 面试官可能怎么问

你这个项目已经有浏览器 WebRTC 了，为什么还要做 native libwebrtc sender？

## 一句话回答

浏览器 WebRTC 能证明我理解上层建连和 stats；native libwebrtc sender 是为了证明我能把 C++ 采集、帧时间戳、像素格式、编码输入和 WebRTC 传输栈连接起来。

## 项目里怎么体现

我把 Phase 3 拆成几层：

1. V4L2 摄像头采集，理解设备 buffer、pixel format 和 timestamp。
2. FFmpeg probe，理解 MP4、PTS/DTS、time_base、GOP、关键帧和 B 帧。
3. H.264 Annex-B/NALU/RTP packetizer，理解编码数据进入 RTP 前的结构。
4. native libwebrtc sender，把 native 帧源送到浏览器端 WebRTC receiver。

## 为什么不直接自己发 RTP

WebRTC 不等于 RTP。WebRTC 的价值还包括 ICE、DTLS-SRTP、RTCP、NACK/PLI、拥塞控制、pacing、Jitter Buffer 和 stats。自己发裸 RTP 可以学习 packetization，但不能代表完整 WebRTC。

所以我的设计是：

- 学习路径：自己实现 H.264 RTP packetizer，讲清楚 NALU、FU-A、timestamp、sequence、marker。
- 工程路径：真正发送时把 raw frame 交给 libwebrtc，让它负责编码、RTP/RTCP、SRTP 和网络自适应。

## 最小 Demo 怎么做

第一版 native sender 不急着接摄像头，也不急着播放素材，而是先用 synthetic I420 图案。

链路：

```text
Synthetic I420 frame
  -> Custom VideoSource
  -> VideoTrack
  -> PeerConnection
  -> SDP / ICE signaling
  -> Browser receiver
  -> stats dashboard
```

这样可以先验证 libwebrtc 建连、track 发送和浏览器接收。成功后再把输入源替换为：

```text
FRXXZ.mp4 -> FFmpeg decode -> I420 frame
```

最后再替换为：

```text
/dev/video0 -> V4L2 capture -> I420 frame
```

## 和过往经验怎么连接

这条路线可以连接我之前做过的能力：

- C++ SDK：native 模块封装、生命周期、线程模型。
- FFmpeg：解封装、解码、PTS/time_base、像素格式转换。
- H.264：SPS/PPS、IDR、GOP、B 帧、RTP FU-A。
- 低延迟预览：帧时间戳、队列长度、render pacing、丢帧策略。
- ABR：WebRTC stats 和编码码率控制可以对应到我之前做过的弱网推流经验。

## 常见坑

1. libwebrtc 构建很重，不能把编译环境问题和项目能力混在一起。
2. PeerConnection callback 不要做耗时工作，否则会阻塞 signaling thread。
3. 帧时间戳必须单调，否则接收端 jitter buffer 和同步会出问题。
4. raw frame 格式要匹配 libwebrtc 输入，I420 是最适合第一版验证的格式。
5. 摄像头在 WSL USB/IP 下可能不稳定，面试演示要保留 synthetic / FRXXZ.mp4 备用输入。

## 面试表达模板

我没有一上来就把 libwebrtc 源码拉下来硬编，而是先把链路拆开验证：浏览器 WebRTC 建连、native 摄像头采集、FFmpeg 时间戳和 GOP、H.264 NALU 和 RTP 分片。这样进入 native libwebrtc 时，我知道每个模块的边界在哪里。

真正的 native sender 我会优先走 raw frame 输入 libwebrtc 的路线，因为它能复用 libwebrtc 的编码适配、RTP/RTCP、SRTP、NACK/PLI、拥塞控制和 pacing。自己写的 RTP packetizer 作为学习和排查工具，用来解释底层发生了什么。
