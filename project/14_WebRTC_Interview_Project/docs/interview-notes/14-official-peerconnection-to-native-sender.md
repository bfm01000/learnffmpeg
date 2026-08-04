# 面试讲解：从官方 peerconnection_client 到自己的 native sender

## 一句话回答

我先编译并拆解官方 peerconnection_client，然后只借鉴 PeerConnection 生命周期和 SDP/ICE 处理，不照搬它的 GTK UI 和 HTTP 长轮询信令，而是接入我项目已有的 WebSocket signaling server。

## 官方示例里最有价值的部分

`Conductor` 是核心：

- 创建 PeerConnectionFactory。
- 创建 PeerConnection。
- 添加 audio/video track。
- 处理 offer/answer。
- 处理 ICE candidate。
- 管理 PeerConnection 清理。

## 为什么不直接改官方示例

官方示例是一个完整 demo，但它有几个不适合本项目的点：

1. Linux 版本依赖 GTK UI。
2. 信令协议是官方 `peerconnection_server` 的 HTTP long-poll。
3. UI、信令和 PeerConnection 生命周期混在一起。
4. 它是通用 demo，不是面向“native sender -> 浏览器 receiver”的最小路径。

所以我选择提炼结构，而不是复制代码。

## 我的最小结构

```text
main
  -> WebSocketSignalingClient
  -> PeerConnectionApp
  -> SyntheticVideoSource
```

这样每个模块都能单独讲清楚：

- `WebSocketSignalingClient`：只负责 join/offer/answer/candidate。
- `PeerConnectionApp`：只负责 libwebrtc API 和状态机。
- `SyntheticVideoSource`：只负责生成 I420 测试帧。

## 和之前项目的连接

之前已经做过：

- 浏览器 WebRTC MVP。
- stats dashboard。
- V4L2 采集。
- FFmpeg PTS/DTS/GOP 分析。
- H.264 Annex-B/NALU/RTP packetizer。
- libwebrtc 官方示例构建。

现在进入 native sender，就是把这些能力收束到一个可演示闭环：

```text
native C++ frame source
  -> libwebrtc
  -> browser receiver
  -> stats dashboard
```

## 面试表达模板

我没有盲目魔改官方 demo，而是先编译通过官方 peerconnection 示例，确认 libwebrtc 构建链路可靠。然后我拆解了它的 Conductor、信令 client 和 Linux UI，决定只复用 PeerConnectionFactory、AddTrack、SDP/ICE 处理这些核心思路。我的项目会重写 headless main、WebSocket signaling 和 synthetic I420 video source，这样更贴近真实 SDK/服务端 sender 的工程结构。