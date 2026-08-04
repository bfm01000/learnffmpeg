# 023 - native low_latency_sender 最小实现

## 本次做了什么

- 新增 native libwebrtc sender 源码，放在 `native/libwebrtc_sender/`。
- 将源码同步到 WebRTC checkout 的 `src/examples/low_latency_sender/`，并在 `examples/BUILD.gn` 注册 `low_latency_sender` target。
- 成功编译出 `out/Default/low_latency_sender`。
- 修改页面信令，让浏览器和 native sender 都带 role，支持定向 offer。
- 修改 native sender 默认房间为 `lab`，和页面默认房间保持一致。
- 新增 `scripts/sync_low_latency_sender_to_webrtc.sh`，以后 gclient 同步覆盖后可以一键恢复目标。
- 修改 `scripts/build_webrtc_examples.sh`，GN 参数加入 `rtc_include_pulse_audio=false`。

## 遇到的问题

1. native sender 首次运行崩溃：

   WebRTC 默认初始化 PulseAudio，WSL 环境下在 `audio_device_pulse_linux.cc` 触发 `thread_checker_.IsCurrent()` fatal。

2. native sender 和页面默认房间不一致：

   页面默认是 `lab`，native 默认是 `native-demo`，容易导致双方都启动了但互相看不到。

3. 页面只处理“新 peer 加入”场景：

   如果浏览器先进入房间，native 后加入，浏览器会发 offer；但如果 native 先进入房间，浏览器加入后原逻辑不会对已有 peer 主动发 offer。

4. 后台进程启动方式不稳定：

   普通 `nohup ... &` 在当前 WSL 调用环境中有时会随着 shell 结束被带走，导致 native 报连接 server 失败。

## 怎么解决

- 通过 GN 参数关闭 PulseAudio：

  ```bash
  gn gen out/Default --args='is_debug=true rtc_include_tests=false treat_warnings_as_errors=false rtc_include_pulse_audio=false'
  ```

- 将 native 默认房间改为 `lab`。
- 给信令 join 增加 `role`，server 返回 peer 列表时带 `{ id, role }`。
- 浏览器发现已有 `native-sender` 时主动发送定向 offer；收到 `peer-joined` 时也对指定 peer 发 offer。
- 使用 `setsid` 启动后台 server/native，验证 server 监听和 native TCP 连接：

  ```bash
  ss -tnp | grep ':3000'
  ```

## 验证结果

- `node --check server.js` 通过。
- `node --check public/app.js` 通过。
- `autoninja -C out/Default examples/low_latency_sender:low_latency_sender` 通过。
- `low_latency_sender --help` 显示默认房间 `lab`。
- server 已监听 `0.0.0.0:3000`。
- native sender 与 server 已建立 TCP 连接。

## 面试讲法

这一步可以讲成：我先没有急着做完整采集和编码，而是把 native libwebrtc 接入浏览器这条最关键链路拆出来。具体做法是：C++ 进程创建 PeerConnection 和 synthetic video track，通过自研 WebSocket 信令服务和浏览器交换 SDP/ICE；浏览器负责 offer，native 负责 answer。这样先验证 WebRTC 原生侧线程模型、factory 初始化、track 注入、信令转发和浏览器互通。过程中遇到 WSL PulseAudio 初始化崩溃，我通过阅读 WebRTC GN 参数定位到 `rtc_include_pulse_audio`，关闭后让音频设备走 ALSA 路径，因为当前阶段只需要视频，这个取舍能解释清楚。

## 下一步

- 请在浏览器刷新 `http://localhost:3000`，房间保持 `lab`，点击 Join，看 Remote Video 是否出现 native synthetic 画面。
- 画面确认后，下一步把 synthetic source 替换成真实输入源：V4L2 摄像头或 FFmpeg 解码帧。