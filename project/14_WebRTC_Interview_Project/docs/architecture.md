# 架构说明

## 当前 Phase

当前实现是 Phase 1 浏览器 WebRTC MVP，目标是跑通最小实时音视频闭环，并建立可观测的 stats 基线。

```text
Browser A
  getUserMedia
  RTCPeerConnection
  local stats
        |
        | WebSocket signaling
        v
Node.js signaling server
  static files
  room registry
  offer / answer relay
  ICE candidate relay
        ^
        | WebSocket signaling
        |
Browser B
  getUserMedia
  RTCPeerConnection
  remote video
  local stats
```

媒体数据不经过 Node.js 服务。Node.js 只负责控制面信令，真正的视频数据由浏览器 WebRTC 栈通过 ICE 建立 P2P 连接后传输。

## 模块拆分

### `server.js`

职责：

1. 提供静态文件服务。
2. 完成 WebSocket 握手。
3. 管理 client id 和 room。
4. 转发 `offer`、`answer`、`candidate`。
5. 广播 peer join / leave 事件。

当前没有引入 `ws` 依赖，而是手写最小 WebSocket 文本帧处理，原因是 MVP 阶段要减少依赖，确保在网络受限环境也能运行。

### `public/app.js`

职责：

1. 调用 `getUserMedia` 获取摄像头视频。
2. 创建 `RTCPeerConnection`。
3. 将本地 track 添加到连接。
4. 通过 WebSocket 与信令服务交换 SDP 和 ICE candidate。
5. 处理远端 track 并播放。
6. 周期性读取 `getStats()` 并更新 UI。

### `public/index.html` 和 `public/styles.css`

职责：

1. 提供房间、分辨率、fps 控制。
2. 展示本地和远端视频。
3. 展示 WebRTC stats。
4. 展示事件日志，方便观察建连过程。

## 建连流程

1. 两个浏览器页面连接 WebSocket。
2. 两端加入同一个 room。
3. 新 peer 加入时，已有 peer 收到 `peer-joined`。
4. 已有 peer 创建 SDP offer 并发送给新 peer。
5. 新 peer 设置 remote offer，创建 answer 并返回。
6. 双方持续交换 ICE candidate。
7. ICE 成功后，媒体数据开始通过 WebRTC 传输。
8. `ontrack` 收到远端视频流并播放。

## 当前限制

1. 当前只按双人房间设计，多人房间需要 SFU 或更完整的 peer 管理。
2. 信令服务只支持 MVP 文本帧，没有处理复杂 WebSocket 分片。
3. 当前没有 TURN 服务，复杂 NAT 环境下可能无法直连。
4. 当前没有音频，便于聚焦视频链路和 stats。
5. stats 目前是即时数值，后续需要趋势图和采样记录。

## 和后续 C++ / FFmpeg 的关系

当前浏览器 MVP 先把 WebRTC 控制面和传输面跑通。后续可以把采集入口从浏览器 `getUserMedia` 扩展到：

1. V4L2 C++ 摄像头采集。
2. FFmpeg 读取 MP4 或摄像头。
3. native libwebrtc sender。

这条路线和作者已有的实时预览、RTMP 推流、AJB、ABR、零拷贝经验是连续的：先观测链路，再定位瓶颈，最后逐步替换采集、编码和发送模块。
