# Dev Log 021 - 官方 peerconnection_client 拆解

## 本次目标

在官方 `peerconnection_client` 已经编译成功后，阅读核心源码，提炼本项目 `native/libwebrtc_sender` 的最小实现结构。

## 做了什么

1. 阅读 `examples/peerconnection/client/conductor.h`。
2. 阅读 `examples/peerconnection/client/conductor.cc`。
3. 阅读 `examples/peerconnection/client/peer_connection_client.h/.cc`。
4. 阅读 `examples/peerconnection/client/linux/main.cc` 和部分 `main_wnd.cc`。
5. 分析官方示例中 PeerConnection、信令、UI、视频源的职责边界。
6. 输出 `docs/phase3/official-peerconnection-client-analysis.md`。
7. 输出 `docs/phase3/native-sender-minimal-structure.md`。
8. 输出 `native/libwebrtc_sender/BUILD.gn.template`。
9. 输出面试讲解 `docs/interview-notes/14-official-peerconnection-to-native-sender.md`。

## 关键发现

### Conductor 是核心

官方 `Conductor` 负责创建 PeerConnectionFactory、PeerConnection、AddTrack、处理 SDP/ICE、创建 answer/offer。它是最值得借鉴的部分。

### 官方信令不适合直接复用

`PeerConnectionClient` 使用官方 HTTP long-poll signaling server，而本项目已经有 WebSocket signaling server。复用官方信令会导致两套协议并存，不利于项目主线表达。

### GTK UI 不适合本项目

Linux 示例把 GTK event loop 和 WebRTC socket server 放在一起。我们的第一版 native sender 应该是 headless CLI，更接近 SDK/服务端 sender 的形态。

## 技术取舍

第一版 native sender 设计为 answerer：浏览器先加入房间并发 offer，native 后加入并回答 answer。这样不需要修改现有 `server.js` 和 `public/app.js`。

第一版只发 synthetic I420 video，不接摄像头和 FFmpeg 素材。这样可以先验证 libwebrtc 发送链路，减少变量。

## 遇到的问题

这一步主要是源码分析，没有新的构建问题。需要注意的是 WebRTC GN deps 不能凭记忆写死，后续实现时要用 `gn desc` 和官方 examples 的 BUILD.gn 实际校准。

## 面试可讲点

我不是简单复制官方 demo，而是拆清楚它的职责：UI、信令、PeerConnection 生命周期、video source。然后我保留核心 WebRTC API 调用路径，替换成我项目已有的 WebSocket signaling 和 headless synthetic sender，这更符合工程项目的目标。

## 下一步

开始实现 `native/libwebrtc_sender` 的第一版源码，并同步到 WebRTC checkout 的 `src/examples/low_latency_sender` 里建立 GN target。