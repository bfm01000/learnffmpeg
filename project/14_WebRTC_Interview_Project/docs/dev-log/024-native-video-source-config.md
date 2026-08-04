# 024 - native sender 视频源参数化

## 本次做了什么

- 给 native sender 新增 `VideoSourceConfig`。
- `main.cpp` 支持解析：
  - `--source`
  - `--width`
  - `--height`
  - `--fps`
- `SyntheticTrackSource` 不再写死 640x480@30fps，而是根据命令行配置创建测试帧。
- `PeerConnectionApp::AddVideoTrack()` 返回 bool，初始化失败可以向上返回错误。
- native 日志增加 `std::flush`，后台运行时能更快看到 joined/source/offer/answer 等状态。

## 遇到的问题

当前还没有直接接 V4L2/FFmpeg，因为这会引入新的帧格式转换问题。直接一步接摄像头容易把两个问题混在一起：

1. WebRTC native PeerConnection 是否稳定；
2. 外部采集帧如何转成 WebRTC `VideoFrame`。

## 怎么解决

本次先做边界拆分：

- PeerConnection 和信令继续保持稳定。
- 视频输入先通过 `VideoSourceConfig` 参数化。
- 真实摄像头作为下一步 source 实现接入。

这样后续出现问题时，可以明确判断是 WebRTC 协商问题，还是采集/像素格式/时间戳问题。

## 验证结果

运行命令：

```bash
./out/Default/low_latency_sender \
  --host 127.0.0.1 \
  --port 3000 \
  --room lab \
  --source synthetic \
  --width 320 \
  --height 240 \
  --fps 15
```

验证日志：

```text
Synthetic video track added: 320x240@15fps
low_latency_sender joined room 'lab' with source=synthetic 320x240@15fps
signaling hello: ...
joined room: lab
```

同时 `ss -tnp | grep ':3000'` 显示 native sender 和 Node server 已建立连接。

## 面试讲法

这一步可以讲“工程边界意识”：我先把 WebRTC 发送链路稳定下来，再把视频源参数化。这样后面接摄像头或 FFmpeg 时，不会污染 PeerConnection 的生命周期管理。面试官如果追问扩展方式，可以说明：后续只需要实现新的 `VideoTrackSourceInterface` 或帧推送 source，把 V4L2/FFmpeg 产出的帧封装成 WebRTC `VideoFrame`。