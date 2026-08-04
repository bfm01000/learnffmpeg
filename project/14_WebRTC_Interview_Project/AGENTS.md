# Codex Project Guide

这个文件是 Codex 在本项目中的长期工作规则，作用类似 `CLAUDE.md`。每次在本目录工作时，先读本文件，再读当前目录结构和已有文档。

## 项目一句话

**LowLatency WebRTC Lab** 是一个面向求职和面试表达的实时音视频工程项目：先跑通 WebRTC，再通过 stats、弱网、码率控制、延迟测量、FFmpeg / V4L2 / C++ / native WebRTC 深入到底层链路。

## 作者背景

作者是 C++ / 跨平台 SDK / 实时音视频方向工程师，曾在 Insta360 做移动端 C++ SDK 开发。

重点经历：

1. C++、CMake、多线程、线程池、锁、任务调度、性能分析。
2. FFmpeg 解复用、解码、编码、复用链路。
3. H.264 / H.265、MediaCodec、VideoToolbox、AHardwareBuffer / Surface。
4. Android / iOS SDK 封装，JNI、Objective-C++、KMP。
5. 4K RTMP 推流、弱网掉帧、ABR、异步发送队列。
6. 低延迟实时预览，端到端延迟从约 1.5s 优化到 0.5s~0.6s。
7. 自适应抖动缓冲 AJB、动态追帧、render pacing、零拷贝链路。
8. 视频时间模型，int64 pts、time_base、frame index、精准 Seek、Smart Seek。

Codex 写方案、代码和文档时，要主动把 WebRTC 与这些经历连接起来。

## 项目原则

1. 先做可运行闭环，再做架构深化。
2. 代码、验证、文档、面试讲法同步推进。
3. 不做纯理论笔记，也不做只会调用 API 的 Demo。
4. 每个阶段都要有明确验收标准。
5. 每个关键问题都要记录排查过程和技术取舍。
6. 保持工程结构简单可靠，避免过早引入复杂抽象。
7. 不删除用户已有内容，不覆盖无关修改。

## 阶段路线

### Phase 0：环境和采集链路

目标：确认 WSL / Linux 摄像头采集可用。

已做或优先做：

1. 检查 `/dev/video*`。
2. 使用 FFmpeg 枚举摄像头格式。
3. 使用 `ffplay` 预览摄像头。
4. 使用 FFmpeg 抓帧保存 JPG。
5. 记录 `docs/dev-log/001-camera-preview-check.md`。

### Phase 1：浏览器 WebRTC MVP

目标：跑通 WebRTC 建连和基础通话。

交付物：

1. Node.js WebSocket signaling server。
2. 浏览器客户端，支持房间、offer / answer、ICE candidate。
3. 本地和远端视频展示。
4. 基础 stats 面板：RTT、jitter、packet loss、bytes sent / received、fps、resolution。
5. `README.md`、`docs/architecture.md`、`docs/interview-notes/01-webrtc-call-flow.md`。
6. 对应 `docs/dev-log/`。

### Phase 2：实验台能力

目标：让项目能分析实时音视频问题。

候选模块：

1. stats dashboard。
2. 编码参数控制：resolution、fps、maxBitrate。
3. 端到端延迟测量。
4. 弱网模拟：loss、delay、jitter、bandwidth limit。
5. 策略实验：固定码率、简单 ABR、主动降帧、降分辨率。

### Phase 3：C++ / FFmpeg / native 深入

目标：把作者已有 C++ 和编解码经验迁移到 WebRTC。

候选模块：

1. V4L2 C++ 摄像头采集 Demo。
2. FFmpeg MP4 分析工具，解释 PTS、DTS、time_base、GOP、关键帧、B 帧。
3. FFmpeg 解码 / 编码帧进入实验链路。
4. libwebrtc native sender / receiver 调研和最小 Demo。
5. RTP / RTCP / jitter buffer / NACK / PLI 实验。
6. WebRTC vs RTMP 对比文档。

## 技术路线偏好

1. MVP 优先使用 Node.js + WebSocket + 浏览器 WebRTC API。
2. 前端可以用原生 HTML / CSS / JavaScript；若引入 Vite 或框架，要说明理由。
3. 脚本优先使用 Bash，便于 WSL / Linux 使用。
4. C++ 模块优先使用 CMake，代码保持小步可验证。
5. 第三方依赖要克制，新增依赖必须说明收益。
6. 对摄像头、FFmpeg、V4L2、WebRTC stats 等链路，优先写可复现脚本。

## 代码风格

1. 代码命名使用英文。
2. 文档和解释使用中文。
3. 代码优先清晰、直接、可调试。
4. 只有在降低复杂度或符合局部模式时才增加抽象。
5. 对 WebRTC 状态机、stats 解析、弱网策略、时间戳处理写必要注释。
6. 前端页面做成工程实验台，不做营销页。
7. UI 要便于观察指标、调整参数、复现实验。

## 文档结构

推荐结构：

```text
README.md
PROMPT.md
AGENTS.md
scripts/
  preview_camera.sh
  capture_camera_jpg.sh
captures/
docs/
  architecture.md
  roadmap.md
  dev-log/
    000-template.md
    001-camera-preview-check.md
  interview-notes/
    01-webrtc-call-flow.md
    02-sdp-and-codec.md
    03-ice-stun-turn.md
    04-rtp-rtcp-jitter-buffer.md
    05-webrtc-vs-rtmp.md
    06-congestion-control-and-abr.md
```

## 开发过程日志要求

每完成一个关键步骤，都必须在 `docs/dev-log/` 下新增或更新一篇过程日志。

日志文件命名使用递增编号，例如：

```text
docs/dev-log/001-camera-preview-check.md
docs/dev-log/002-prompt-agents-optimization.md
docs/dev-log/003-signaling-server.md
docs/dev-log/004-webrtc-call-mvp.md
```

每篇日志必须包含：

1. 本次目标。
2. 做了什么。
3. 遇到的问题。
4. 怎么排查。
5. 怎么解决。
6. 技术取舍。
7. 涉及的关键知识点。
8. 和我过往经验的连接。
9. 面试讲法。
10. 后续可扩展。

日志不是流水账。重点写清楚“为什么这样设计、遇到问题如何定位、最后做了什么取舍”。如果本次只是很小的格式修正，可以不新增日志，但最终回复必须说明原因。

## 面试笔记要求

当功能涉及 WebRTC 原理或音视频链路时，要更新 `docs/interview-notes/`。

每篇面试笔记建议包含：

1. 面试官可能怎么问。
2. 一句话回答。
3. 项目里怎么体现。
4. 底层原理。
5. 和作者过往经历的连接。
6. 常见坑和排查方式。
7. 可继续深入方向。

优先沉淀的问题：

1. WebRTC 建连流程：getUserMedia、RTCPeerConnection、SDP offer / answer、ICE candidate。
2. ICE、STUN、TURN 分别解决什么问题。
3. RTP、RTCP、SRTP、DTLS 的关系。
4. SDP 如何影响编解码、传输和协商。
5. jitter buffer 的作用和过大过小的影响。
6. WebRTC 拥塞控制与直播 ABR 的差异。
7. 如何定位卡顿、花屏、延迟、音画不同步。
8. RTMP 与 WebRTC 在延迟、可靠性、互动性、NAT 穿透、拥塞控制上的差异。
9. 编码参数、GOP、B 帧、码率、帧率、分辨率如何影响实时体验。
10. 作者已有 AJB、ABR、零拷贝、4K 推流经验如何迁移到 WebRTC。


## 统一素材约定

后续所有需要视频素材的实验、文档和验证，默认统一使用：

```text
/home/bfm01000/workspace/video_downloads/FRXXZ.mp4
```

只有当该素材无法满足某个特定实验时，才可以临时使用其他素材，并且必须在 dev-log 中说明原因。
## 每次工作流程

开始时：

1. 读 `AGENTS.md`。
2. 查看目录结构和相关文件。
3. 判断本次属于哪个 Phase。
4. 简短说明要做什么。

实现时：

1. 小步提交式修改，优先可运行。
2. 不只给方案，除非用户明确要求只讨论。
3. 遇到环境、依赖或权限问题，先排查并记录。
4. 不引入无关重构。

结束前：

1. 运行可行的验证命令。
2. 更新 `docs/dev-log/`。
3. 必要时更新 `docs/interview-notes/`。
4. 最终回复说明：改了什么、怎么运行、验证结果、面试怎么讲。

## 当前下一步建议

如果用户没有指定更具体任务，优先推进 Phase 1：

1. 创建 WebRTC MVP 项目结构。
2. 实现 Node.js WebSocket 信令服务。
3. 实现浏览器 WebRTC 通话页面。
4. 实现基础 stats 面板。
5. 编写 README、架构文档、建连流程面试笔记和过程日志。

