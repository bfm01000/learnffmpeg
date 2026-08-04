# 07 Stats 和 DataChannel 延迟测量

## 面试官可能怎么问

1. WebRTC 出现卡顿或延迟时，你怎么定位？
2. `getStats()` 能看到哪些关键指标？
3. RTT、jitter、packet loss、bitrate 分别说明什么？
4. 为什么还要用 DataChannel 做 ping-pong？

## 一句话回答

`getStats()` 用来观察 WebRTC 媒体和网络状态，DataChannel ping-pong 用来补充应用层往返延迟；两者结合可以把“感觉卡”变成可量化、可对比、可复现的问题。

## 项目里怎么体现

当前项目每秒调用 `RTCPeerConnection.getStats()`，展示并绘制：

1. candidate-pair RTT。
2. inbound RTP jitter。
3. packet loss。
4. send / receive bitrate。
5. fps 和 resolution。

同时在 offer 端创建 `latency` DataChannel，通过 `ping` / `pong` 消息计算应用层 Data RTT，并显示到页面。

## 底层原理

`getStats()` 是浏览器 WebRTC 栈暴露的统计接口，数据来自 ICE、RTP / RTCP、编码器、解码器和传输层。candidate-pair RTT 更接近 ICE 选中网络路径上的往返时间，jitter 反映 RTP 到达间隔抖动，packet loss 反映网络丢包或接收端统计到的丢失。

DataChannel 基于 SCTP over DTLS，和媒体流共享 PeerConnection 的底层传输路径。用 ping-pong 可以测到应用层消息从一端 JS 发出，到对端 JS 收到并回包，再回到本端 JS 的往返耗时。它不是严格的视频端到端延迟，但很适合验证控制消息延迟和链路状态。

## 和我过往经验的连接

这对应以前做实时预览和 RTMP 推流时的全链路埋点。以前会看网络接收、解码队列、渲染队列、水位、丢帧和码率变化；WebRTC 中可以先从 stats 看到 RTT、jitter、loss、bitrate、fps，再进一步扩展到发送端时间戳、接收端渲染时间和端到端玻璃到玻璃延迟。

## 常见坑和排查方式

1. RTT 正常但画面卡：继续看 jitter、fps、decode、render 或主线程压力。
2. jitter 高：说明包到达间隔不稳定，jitter buffer 可能增大导致延迟上升。
3. packet loss 高：可能触发 NACK / PLI / 降码率，画面可能糊或卡。
4. Data RTT 高但 candidate RTT 不高：可能是 JS 主线程、DataChannel 队列或浏览器调度问题。
5. send bitrate 很高但 recv bitrate 很低：排查网络、拥塞控制、接收端解码能力。

## 可继续深入方向

1. 保存 stats 采样为 JSON，用于离线分析。
2. 增加端到端视频延迟测量，把时间戳绘制进画面。
3. 增加编码参数控制，观察码率和 fps 如何变化。
4. 增加弱网模拟，对比固定码率、降帧、降分辨率和简单 ABR。
