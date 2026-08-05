# 027 - 补充 PeerConnection/native sender 入门解释文档

## 本次做了什么

用户反馈“有点看不懂”，并追问 `PeerConnection` 来自哪里。因此新增一篇面向初学者的完整解释文档：

- `docs/phase3/peerconnection-native-sender-explained-for-beginners.md`

## 文档解决的问题

- 解释 `PeerConnection` 是什么。
- 解释它来自 Google libwebrtc，不是项目自己实现的。
- 解释 `PeerConnectionFactory -> PeerConnectionInterface -> VideoTrack` 的关系。
- 解释 FRXXZ.mp4 如何经过 FFmpeg/I420/VideoFrame/VideoTrack 进入 WebRTC。
- 解释 WebSocket 信令和 RTP/SRTP 视频传输的区别。
- 提供可以直接用于面试的讲法。

## 面试讲法

如果被问到 PeerConnection，可以回答：

> PeerConnection 来自 Google libwebrtc。我没有自己实现 WebRTC 协议栈，而是通过 `CreateModularPeerConnectionFactory` 创建 factory，再通过 `CreatePeerConnectionOrError` 创建 `PeerConnectionInterface`。我自己实现的是 native sender glue code：WebSocket 信令、SDP/ICE 处理、VideoTrack 添加、I420 视频帧推送。