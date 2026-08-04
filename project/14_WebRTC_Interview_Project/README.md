# LowLatency WebRTC Lab

面向求职和面试表达的 WebRTC 实时音视频实验项目。当前处于 Phase 1 MVP：用 Node.js 信令服务和浏览器 WebRTC API 跑通两端视频通话，并展示基础 stats。

## 当前能力

1. Node.js HTTP 静态服务。
2. 无第三方依赖的 WebSocket 信令服务。
3. 房间加入、offer / answer / ICE candidate 转发。
4. 浏览器摄像头采集和 P2P 视频通话。
5. 基础 WebRTC stats：RTT、jitter、packet loss、发送/接收码率、fps、分辨率、ICE 状态。
6. DataChannel ping-pong 延迟测量。
7. 发送/接收码率、RTT、jitter 趋势图。
8. 通过 `RTCRtpSender.setParameters()` 动态调整 video sender 的 `maxBitrate` 和 `maxFramerate`。`r`n9. Simple ABR 策略：根据 RTT、jitter、packet loss 自动降码率，稳定后缓慢升码率。
9. 导出本次会话 stats JSON，用于离线分析和弱网对比。
6. 摄像头不可用时自动 fallback 到 Canvas Test Pattern，便于隔离验证 WebRTC 建连和传输。

## 运行

```bash
cd ~/workspace/learnffmpeg/project/14_WebRTC_Interview_Project
npm start
```

浏览器打开：

```text
http://localhost:3000
```

打开两个浏览器窗口或两个标签页，输入相同房间号，例如 `lab`，分别点击 `Join`。第二个页面加入后，第一个页面会创建 offer，双方通过信令服务交换 SDP 和 ICE candidate，连接成功后可以看到远端画面。

## 验证命令

```bash
npm run check
node --check public/app.js
```

## 摄像头辅助脚本

WSL 摄像头预览：

```bash
./scripts/preview_camera.sh
```

保存一张 JPG：

```bash
./scripts/capture_camera_jpg.sh
```

## 面试讲法

这个 MVP 可以这样讲：

我先用浏览器 WebRTC API 跑通最小实时音视频闭环，但没有只停留在 API 调用。我自己实现了一个轻量 WebSocket 信令服务，负责房间管理和 offer / answer / ICE candidate 转发。浏览器端建立 RTCPeerConnection，采集本地摄像头，添加 track，收到远端 track 后播放，同时周期性读取 getStats，把 RTT、jitter、packet loss、码率、fps、分辨率展示出来。这样后续做弱网、ABR、jitter buffer 和延迟优化时，有一个可观测的基线。

## 下一步

1. 增加画面时间戳端到端视频延迟测量。
2. 增加弱网模拟和策略对比。
3. 增加 stats JSON 离线分析脚本。
4. 增加摄像头设备选择和更细的采集错误提示。
5. 引入 C++ / FFmpeg / V4L2 / native libwebrtc 深入实验。






