# native/libwebrtc_sender 最小结构设计

## 第一版目标

实现一个 headless native C++ sender：

```text
Synthetic I420 frame
  -> libwebrtc VideoTrackSource
  -> PeerConnection answerer
  -> existing WebSocket signaling server
  -> browser remote video
```

第一版只追求浏览器能看到 native 发来的动态图案。

## 为什么先做 answerer

当前浏览器页面逻辑是：

```text
join room
  -> peer-joined
  -> browser createOffer
  -> browser send offer
```

因此最小改动路径是：

```text
browser first join
native second join
native receives offer
native creates answer
```

这样不需要改 `server.js` 和 `public/app.js`。

## 推荐源码结构

项目目录保留源码草稿：

```text
native/libwebrtc_sender/
  README.md
  BUILD.gn.template
  main.cpp
  websocket_signaling_client.h
  websocket_signaling_client.cpp
  peer_connection_app.h
  peer_connection_app.cpp
  synthetic_video_source.h
  synthetic_video_source.cpp
```

实际编译时，第一版建议把这些文件同步/复制到 WebRTC checkout：

```text
/home/bfm01000/workspace/third_party/webrtc-checkout/src/examples/low_latency_sender/
```

原因：WebRTC 是 GN/Ninja 工程，直接在本项目 CMake 中链接 libwebrtc 产物会过早引入复杂度。

## 模块职责

### `main.cpp`

职责：

1. 解析参数：server、room、fps、width、height。
2. 初始化 WebRTC SSL / Environment。
3. 创建 signaling client。
4. 创建 PeerConnectionApp。
5. 连接 signaling server。
6. 阻塞运行直到 Ctrl+C。

### `websocket_signaling_client`

职责：

1. 建立 WebSocket 连接。
2. 发送 `join`。
3. 接收 `hello / joined / offer / answer / candidate / peer-left`。
4. 将 offer/candidate 回调给 `PeerConnectionApp`。
5. 发送 answer/candidate。

注意：第一版可以先实现最小 WebSocket frame，不引入第三方库；如果复杂度上升，再考虑 websocketpp。

### `peer_connection_app`

职责：

1. 创建 `PeerConnectionFactory`。
2. 创建 `PeerConnection`。
3. 添加 synthetic video track。
4. 处理 remote offer。
5. 创建 answer。
6. 处理 ICE candidate。
7. 将本地 answer/candidate 交给 signaling client 发送。

### `synthetic_video_source`

职责：

1. 按 fps 生成 I420 frame。
2. 维护递增 frame index 和 timestamp。
3. 画面中体现移动条纹或色块，便于肉眼确认不是静态图片。
4. 将 `webrtc::VideoFrame` 推给 WebRTC video source/sink 链路。

## 最小信令 JSON

native 收到浏览器 offer：

```json
{
  "type": "offer",
  "from": "browser-client-id",
  "sdp": {
    "type": "offer",
    "sdp": "..."
  }
}
```

native 回 answer：

```json
{
  "type": "answer",
  "room": "native-demo",
  "target": "browser-client-id",
  "sdp": {
    "type": "answer",
    "sdp": "..."
  }
}
```

candidate：

```json
{
  "type": "candidate",
  "room": "native-demo",
  "target": "browser-client-id",
  "candidate": {
    "candidate": "candidate:...",
    "sdpMid": "0",
    "sdpMLineIndex": 0
  }
}
```

## 最小 GN 目标草案

```gn
rtc_executable("low_latency_sender") {
  testonly = true
  sources = [
    "main.cpp",
    "websocket_signaling_client.cpp",
    "websocket_signaling_client.h",
    "peer_connection_app.cpp",
    "peer_connection_app.h",
    "synthetic_video_source.cpp",
    "synthetic_video_source.h",
  ]

  deps = [
    "//api:create_peerconnection_factory",
    "//api:peer_connection_interface",
    "//api:scoped_refptr",
    "//api:libjingle_peerconnection_api",
    "//api/video:video_frame",
    "//api/video:video_rtp_headers",
    "//api/video_codecs:builtin_video_decoder_factory",
    "//api/video_codecs:builtin_video_encoder_factory",
    "//rtc_base:checks",
    "//rtc_base:logging",
    "//rtc_base:threading",
    "//third_party/jsoncpp",
  ]
}
```

实际 deps 需要根据当前 WebRTC 版本用 `gn refs` / `gn desc` 调整。

## 验收标准

1. 浏览器打开 `http://localhost:3000/` 并加入 `native-demo` 房间。
2. native sender 加入同一房间。
3. 浏览器 remote video 出现 native synthetic 动态画面。
4. 浏览器 stats 显示 inbound-rtp video。
5. native 端日志能看到 joined、offer received、answer sent、candidate sent、connection state。

## 之后如何连接素材和摄像头

第一版 synthetic 成功后再替换输入源：

```text
SyntheticVideoSource
  -> FFmpegFileVideoSource(FRXXZ.mp4)
  -> V4L2CameraVideoSource(/dev/video0)
```

接口保持一致：输出 `webrtc::VideoFrame`。