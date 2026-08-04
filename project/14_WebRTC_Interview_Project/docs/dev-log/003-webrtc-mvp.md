# 003 WebRTC MVP

## 本次目标

进入 Phase 1，实现一个可运行的 WebRTC MVP：两个浏览器页面加入同一个房间后，可以通过 WebRTC 建立 P2P 视频通话，并展示基础 stats。

## 做了什么

新增 `server.js`，实现 Node.js HTTP 静态服务和最小 WebSocket 信令服务。信令服务负责 client id、room、`offer`、`answer`、`candidate`、peer join / leave。

新增 `public/index.html`、`public/styles.css`、`public/app.js`，实现浏览器端通话页面，包括房间号、分辨率、fps、本地视频、远端视频、事件日志和 stats 面板。

新增 `package.json`，提供 `npm start` 和 `npm run check`。

新增 `README.md`、`docs/architecture.md`、`docs/interview-notes/01-webrtc-call-flow.md`，记录运行方式、架构、建连流程和面试讲法。

## 遇到的问题

当前环境网络受限，如果依赖 `npm install ws`，可能会卡在依赖下载。因此 MVP 阶段不能把可运行性建立在外部包下载之上。

浏览器 WebRTC 的媒体传输不需要 Node.js 转发，但建连前必须有一个控制面通道交换 SDP 和 ICE candidate。

## 怎么排查

先检查 WSL 中 Node.js 和 npm 是否可用，确认 Node 18 已安装。再查看当前项目结构，确认还没有 Phase 1 代码。结合项目规则，选择从最小可运行闭环开始，而不是直接进入 native libwebrtc。

## 怎么解决

用 Node.js 内置 `http`、`fs`、`crypto` 实现静态服务和最小 WebSocket 文本帧处理，避免第三方依赖。浏览器端使用原生 WebRTC API，实现 `getUserMedia`、`RTCPeerConnection`、offer / answer、ICE candidate、`ontrack` 和 `getStats()`。

## 技术取舍

当前没有直接引入 `ws`、Vite、React 或 libwebrtc。这样做的好处是 MVP 小、可读、可运行，坏处是手写 WebSocket 只覆盖了当前信令所需的简单文本帧，不适合作为生产实现。后续如果项目复杂化，可以换成成熟 WebSocket 库。

当前也没有做音频和多人房间，因为本阶段目标是先把视频链路和 stats 跑通。多人通话后续更适合引入 SFU，而不是在 P2P MVP 里过度扩展。

## 涉及的关键知识点

1. WebSocket 信令。
2. room 和 peer 生命周期。
3. SDP offer / answer。
4. ICE candidate 交换。
5. `getUserMedia` 摄像头采集。
6. `RTCPeerConnection` 媒体传输。
7. `getStats()` 指标观测。
8. RTT、jitter、packet loss、bitrate、fps、resolution。

## 和我过往经验的连接

信令服务对应 SDK 控制面设计：它不处理媒体数据，只负责状态和协商消息。stats 面板对应之前做低延迟预览和 RTMP 推流时的全链路埋点思想。后续弱网实验中的 jitter、packet loss、bitrate 可以继续连接到 AJB、动态追帧和 ABR 经验。

## 面试讲法

我先实现了一个 WebRTC MVP，用 Node.js 写了轻量 WebSocket 信令服务，只负责房间管理和 offer / answer / ICE candidate 转发，媒体数据完全由浏览器 WebRTC 栈 P2P 传输。浏览器端通过 getUserMedia 采集摄像头，把 track 加到 RTCPeerConnection，收到远端 track 后播放，同时周期性读取 getStats 展示 RTT、jitter、丢包、码率、fps 和分辨率。这个 MVP 的价值是建立一个可观测基线，后面做弱网、码率控制、延迟优化时，不是凭感觉调参，而是能通过指标定位问题。


## 验证结果

已运行 
pm run check 和 
ode --check public/app.js，Node 语法检查通过。已短暂启动 
ode server.js 并用 HTTP 请求 http://127.0.0.1:3000，首页可以正常返回。真实 WebRTC 通话仍需要在浏览器中打开两个页面进行摄像头授权和 P2P 建连验证。

## 后续可扩展

1. 增加 stats 趋势图和采样记录。
2. 增加 maxBitrate、fps、resolution 动态控制。
3. 增加 DataChannel ping-pong 端到端延迟测量。
4. 增加弱网模拟和策略对比。
5. 进入 C++ / FFmpeg / V4L2 / native libwebrtc 实验。

## 本次补充：服务保活

在 Codex / Windows / WSL 当前调用方式下，直接使用 `nohup ... &` 没有稳定保活。前台短暂运行 `node server.js` 可以正常启动，说明问题不在服务代码，而在后台进程的持有方式。

最终使用 Windows `Start-Process` 启动一个隐藏的 `wsl.exe` 进程，让 `node server.js` 作为该 WSL 进程的前台任务持续运行。已验证 `curl http://127.0.0.1:3000` 能返回首页，进程列表中也能看到对应 Node 服务。

当前访问地址：`http://localhost:3000`。
