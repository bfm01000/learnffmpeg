# 026 - native sender 接入 FRXXZ I420 文件源

## 本次做了什么

- `low_latency_sender` 新增 `--source i420`。
- 新增 `--file input.i420` 参数。
- 实现 `I420FileTrackSource`：
  - 打开 raw I420 文件；
  - 按 `width * height * 3 / 2` 读取帧；
  - 使用 `webrtc::I420Buffer::Copy` 创建 WebRTC buffer；
  - 使用 `webrtc::VideoFrame::Builder` 写入 timestamp；
  - 通过 `webrtc::VideoBroadcaster::OnFrame` 推给 VideoTrack；
  - 文件读到末尾后循环播放。
- 新增脚本：
  - `scripts/prepare_frxxz_i420.sh`
  - `scripts/run_native_i420_sender.sh`
- 生成真实素材 I420 片段：`artifacts/frxxz-320x180-200f.i420`。

## 遇到的问题

1. GN 依赖名写错：一开始加了 `//api/video:i420_buffer`，但 WebRTC 中 `i420_buffer.h` 属于 `//api/video:video_frame` target，不是独立 target。
2. 旧的 synthetic sender 进程还在房间里，容易造成页面端看到多个 peer。后续测试前需要清理旧 sender。

## 怎么解决

- 查 `api/video/BUILD.gn`，确认：
  - `I420Buffer` 在 `rtc_library("video_frame")` 中；
  - `VideoBroadcaster` 是独立 target：`//api/video:video_broadcaster`。
- 删除不存在的 `//api/video:i420_buffer` 依赖，只保留：

```gn
"//api/video:video_frame",
"//api/video:video_broadcaster",
```

- 清理旧 sender，只保留新的 I420 sender。

## 验证结果

启动命令核心参数：

```bash
./out/Default/low_latency_sender \
  --host 127.0.0.1 \
  --port 3000 \
  --room lab \
  --source i420 \
  --file /home/bfm01000/workspace/learnffmpeg/project/14_WebRTC_Interview_Project/artifacts/frxxz-320x180-200f.i420 \
  --width 320 \
  --height 180 \
  --fps 25
```

日志：

```text
I420 file source started: ... 320x180@25fps, frames=200
low_latency_sender joined room 'lab' with source=i420 320x180@25fps
signaling hello: ...
joined room: lab
i420 frames sent: 125
i420 frames sent: 250
```

## 面试讲法

这一节可以讲“我真正把素材帧送进了 WebRTC native pipeline”。这里不是浏览器 canvas，也不是 synthetic generator，而是先由 FFmpeg 解码出 I420，再由 C++ sender 把 I420 变成 `VideoFrame`。关键点包括：I420 三平面布局、每帧大小计算、按 fps 推帧、timestamp 设置、`VideoBroadcaster` 作为 WebRTC source 到 track 的桥。

## 下一步

请刷新页面 `http://localhost:3000`，房间使用 `lab`，点击 Join，看 Remote Video 是否出现 FRXXZ 画面。确认后继续把 I420 source 从 `peer_connection_app.cpp` 拆成独立模块，并开始加入 stats/日志侧的“当前 source 类型、帧数、循环次数”。