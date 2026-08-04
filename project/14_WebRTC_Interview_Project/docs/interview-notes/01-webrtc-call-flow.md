# 01 WebRTC 建连流程

## 面试官可能怎么问

1. WebRTC 两端是怎么建立连接的？
2. offer / answer 是什么？
3. ICE candidate 为什么需要交换？
4. 信令服务传不传媒体数据？
5. getUserMedia、RTCPeerConnection、SDP、ICE 的关系是什么？

## 一句话回答

WebRTC 建连可以理解为：浏览器先采集本地媒体，再通过信令服务交换 SDP 和 ICE candidate，双方协商编解码、传输参数和网络路径，最后通过 DTLS / SRTP 建立安全的 P2P 媒体传输。

## 项目里怎么体现

本项目 Phase 1 MVP 中：

1. `getUserMedia` 获取本地摄像头视频。
2. `RTCPeerConnection` 管理 WebRTC 连接。
3. `addTrack` 把本地视频 track 加入连接。
4. `createOffer` / `createAnswer` 生成 SDP。
5. Node.js WebSocket 信令服务只转发 `offer`、`answer`、`candidate`。
6. `onicecandidate` 收集本地 candidate 并发给对端。
7. `ontrack` 收到远端媒体流并播放。
8. `getStats` 观察 RTT、jitter、packet loss、bitrate、fps、resolution。

## 底层原理

### getUserMedia

负责采集本地设备，例如摄像头和麦克风。当前项目只采集视频，便于先聚焦视频链路。

### RTCPeerConnection

WebRTC 的核心对象，负责 SDP 协商、ICE、DTLS、SRTP、RTP / RTCP、拥塞控制和 stats 暴露。

### SDP offer / answer

SDP 描述双方的媒体能力和传输参数，包括媒体类型、编解码能力、payload type、方向、ICE 信息、DTLS 指纹等。offer 是发起方提出能力集合，answer 是接收方选择和确认。

### ICE candidate

candidate 表示一个可能可用的网络地址。双方不断交换 candidate，ICE 会尝试找到最优可达路径。局域网可能直接连通，复杂 NAT 下可能需要 STUN 或 TURN。

### 信令服务

信令服务不是 WebRTC 标准的一部分，只负责让双方交换控制消息。媒体数据不经过当前 Node.js 服务。

## 和我过往经验的连接

我之前做 RTMP 推流和低延迟预览时，也会把链路拆成采集、编码、发送、网络、接收、解码、渲染。WebRTC 的建连多了 SDP / ICE / DTLS / SRTP 这些协商和安全传输步骤，但排查思路类似：先确认控制面是否成功，再确认媒体面是否有数据，最后看 stats 定位 RTT、jitter、loss、码率和渲染问题。

## 常见坑和排查方式

1. 页面无法获取摄像头：检查浏览器权限、localhost / https 安全上下文。
2. 两端没有远端画面：检查 offer / answer 是否交换，`setRemoteDescription` 是否成功。
3. ICE 失败：检查 candidate 是否转发，是否处于复杂 NAT，是否需要 TURN。
4. 画面卡顿：看 `jitter`、`packetLoss`、`framesPerSecond`、bitrate。
5. stats 为空：确认连接是否进入 connected，是否已经有 inbound / outbound video rtp。

## 可继续深入方向

1. SDP 字段逐项分析。
2. STUN / TURN 实验。
3. RTP / RTCP 包结构分析。
4. WebRTC GCC 拥塞控制分析。
5. 与 RTMP 推流链路做延迟模型对比。
