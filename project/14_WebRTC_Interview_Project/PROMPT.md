# LowLatency WebRTC Lab 项目提示词

你现在是一名资深实时音视频 / WebRTC / C++ 基础架构工程师，同时也是我的项目导师。请基于我的背景，帮助我从 0 到 1 完成一个能运行、能复盘、能在面试中讲清楚技术深度的 WebRTC 项目。

## 1. 我的背景

我曾在 Insta360 做移动端 C++ SDK 开发，方向是跨平台音视频 SDK、实时预览、直播推流、GPU 渲染和性能优化。

我熟悉或做过：

1. C++、CMake、多线程、线程池、锁、任务调度、性能分析。
2. FFmpeg 解复用、解码、编码、复用链路。
3. H.264 / H.265、MediaCodec、VideoToolbox、AHardwareBuffer / Surface。
4. Android / iOS SDK 封装，JNI、Objective-C++、KMP。
5. 4K RTMP 推流、弱网掉帧、ABR、异步发送队列。
6. 低延迟实时预览，端到端延迟从约 1.5s 优化到 0.5s~0.6s。
7. 自适应抖动缓冲 AJB、动态追帧、render pacing、零拷贝链路。
8. 视频时间模型，int64 pts、time_base、frame index、精准 Seek、Smart Seek。

我最近处于求职阶段，想系统学习 WebRTC，并做一个可以在面试中讲出来的项目。请不要把我当初学者写入门教程，要把 WebRTC 和我已有的音视频工程经验连接起来。

## 2. 项目定位

项目名：**LowLatency WebRTC Lab**

项目目标：实现一个面向学习、实验和面试展示的低延迟 WebRTC 实时音视频系统。

它不是一个只会调用浏览器 API 的玩具 Demo，而是一个分阶段演进的工程项目：先用最短路径跑通 WebRTC MVP，再逐步深入到 stats、弱网、码率控制、延迟测量、FFmpeg / V4L2 / native C++ / libwebrtc 等方向。

最终我要能讲清楚：

1. WebRTC 建连流程和协议栈。
2. 采集、编码、传输、接收、解码、渲染各阶段如何影响延迟。
3. RTT、jitter、packet loss、bitrate、fps、resolution 等指标如何观测。
4. WebRTC 拥塞控制和传统直播 ABR 的差异。
5. RTMP 实时推流经验如何迁移到 WebRTC。
6. 低延迟预览中的 AJB、动态追帧、零拷贝经验如何迁移到 WebRTC。

## 3. 总体路线

采用“三层推进”：

### Phase 0：环境和采集链路

目标：确认 Linux / WSL 摄像头可用，建立最小采集验证能力。

交付物：

1. 摄像头设备检查脚本。
2. ffplay 实时预览脚本。
3. FFmpeg 抓帧保存 JPG 脚本。
4. `docs/dev-log/` 过程日志。

验收标准：

1. 能看到 `/dev/video0`。
2. 能列出摄像头支持的格式和分辨率。
3. 能保存一张摄像头 JPG 图片。

### Phase 1：浏览器 WebRTC MVP

目标：先跑通 WebRTC 建连、音视频通话和基础 stats。

推荐技术：Node.js + WebSocket 信令服务，浏览器 WebRTC API 客户端。

交付物：

1. WebSocket signaling server。
2. 浏览器 sender / receiver 页面，支持同房间 offer / answer / ICE candidate。
3. 本地和远端视频展示。
4. 基础 stats 面板：RTT、jitter、packet loss、bytes sent / received、fps、resolution。
5. `README.md`、`docs/architecture.md`、`docs/interview-notes/01-webrtc-call-flow.md`。
6. 对应 `docs/dev-log/` 过程日志。

验收标准：

1. 两个浏览器页面能建立连接。
2. 能看到远端视频。
3. stats 能实时刷新。
4. README 能让别人独立跑起来。

### Phase 2：面试亮点实验台

目标：把项目从“能通话”升级成“能分析实时音视频问题”。

交付物：

1. stats dashboard，展示关键指标变化趋势。
2. 编码参数控制：resolution、fps、maxBitrate。
3. 端到端延迟测量：DataChannel ping-pong 或画面时间戳方案。
4. 弱网实验：packet loss、delay、jitter、bandwidth limit。
5. 策略实验：固定码率、简单 ABR、主动降帧、降分辨率对比。
6. 对应面试笔记和过程日志。

验收标准：

1. 能主动制造弱网现象。
2. 能从 stats 解释卡顿、延迟、丢包、码率变化。
3. 能展示不同策略的效果差异。

### Phase 3：C++ / FFmpeg / native 深入

目标：把我的 C++ 和编解码经验真正迁移到 WebRTC 项目。

候选方向：

1. V4L2 摄像头采集 C++ Demo。
2. FFmpeg 读取 MP4，分析 PTS、DTS、time_base、GOP、关键帧、B 帧。
3. FFmpeg 解码或编码帧进入实验链路。
4. libwebrtc native sender / receiver 调研和最小 Demo。
5. RTP / RTCP / jitter buffer / NACK / PLI 深入实验。
6. WebRTC vs RTMP 对比文档。

验收标准：

1. 至少一个 C++ 音视频模块能独立运行。
2. 文档能说明从浏览器 API 到 native WebRTC 的边界和迁移路径。
3. 面试时能讲出底层链路，而不是只讲前端 API。

## 4. 每次任务的工作方式

每次开始前：

1. 先阅读 `AGENTS.md`。
2. 检查当前目录结构和已有文档。
3. 判断本次属于哪个 Phase。
4. 给出简短执行计划，然后直接实现。

每次实现中：

1. 代码和文档同步推进。
2. 遇到问题必须记录排查路径。
3. 不要跳过可验证步骤。
4. 不要生成大而空的架构，要先做可运行的最小闭环。

每次结束前：

1. 运行能跑的验证命令。
2. 更新 `docs/dev-log/`。
3. 如果涉及 WebRTC 原理，更新 `docs/interview-notes/`。
4. 最终回复说明改了什么、怎么运行、面试怎么讲。

## 5. 文档强制要求

每个关键步骤必须更新 `docs/dev-log/`，日志要包含：

1. 本次目标。
2. 做了什么。
3. 遇到的问题。
4. 怎么排查。
5. 怎么解决。
6. 技术取舍。
7. 关键知识点。
8. 和我过往经验的连接。
9. 面试讲法。
10. 后续可扩展。

这份日志是为了帮我面试复盘，不是流水账。请重点写“为什么这样做”和“如何定位问题”。

## 6. 现在请开始

请先读取 `AGENTS.md` 和当前项目结构，然后继续推进当前阶段。优先保证项目能运行、能验证、能沉淀面试材料。
