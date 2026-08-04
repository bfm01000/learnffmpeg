# 002 提示词和 AGENTS 优化

## 本次目标

优化 `PROMPT.md` 和 `AGENTS.md`，让后续 Codex 更稳定地按照“可运行项目 + 过程日志 + 面试表达”的方式推进 WebRTC 项目。

## 做了什么

重写 `PROMPT.md`，把项目目标整理成 Phase 0 到 Phase 3 的阶段路线，明确每个阶段的交付物和验收标准。

重写 `AGENTS.md`，把它定位成项目长期工作规则，补充了阶段路线、技术路线偏好、文档结构、开发过程日志要求、面试笔记要求和每次工作流程。

## 遇到的问题

原提示词目标很有技术深度，但表述偏宏大，容易让执行过程直接跳到“大型工业级 C++ WebRTC 系统”，不利于当前从可运行闭环逐步推进。

原 `AGENTS.md` 已经包含过程日志要求，但还可以进一步明确：每次任务属于哪个 Phase、完成前必须验证什么、何时更新 dev-log、何时更新 interview-notes。

## 怎么排查

先读取当前两份文件，确认它们的职责边界：`PROMPT.md` 更适合作为启动新任务时的完整提示词，`AGENTS.md` 更适合作为项目目录内长期生效的执行规则。

## 怎么解决

将 `PROMPT.md` 改成面向任务启动的提示词，强调项目背景、阶段路线、交付物、验收标准和文档强制要求。

将 `AGENTS.md` 改成面向 Codex 执行的项目规则，强调每次开始、实现、结束前分别要做什么。

## 技术取舍

没有把第一阶段直接定为 native libwebrtc 或完整 C++ 推流系统，而是保留浏览器 WebRTC MVP 作为 Phase 1。这样可以先快速跑通 WebRTC 协议和 stats，再把 C++ / FFmpeg / V4L2 经验逐步接入，避免一开始被 libwebrtc 编译和 native API 复杂度拖住。

## 涉及的关键知识点

1. WebRTC 学习路线设计。
2. 实时音视频项目分阶段验收。
3. MVP 与 native 深入之间的边界。
4. 过程日志与面试表达沉淀。
5. 浏览器 WebRTC API、stats、弱网、ABR、V4L2、FFmpeg、libwebrtc 的递进关系。

## 和我过往经验的连接

新的提示词刻意把 WebRTC 和作者已有经验连接起来：stats 对应全链路埋点，弱网和 jitter 对应 AJB / 动态追帧，码率控制对应 RTMP ABR，V4L2 / FFmpeg 对应采集和编解码链路，native libwebrtc 对应 C++ SDK 工程能力。

## 面试讲法

我在做这个项目时，没有一上来就堆一个复杂的 native WebRTC 系统，而是把项目拆成可验证的阶段。先验证摄像头采集链路，再跑通浏览器 WebRTC MVP，接着做 stats、弱网、码率和延迟实验，最后再深入 C++ / FFmpeg / native libwebrtc。这样每一步都有可运行结果，也能讲清楚为什么这样演进，以及如何把我之前在 RTMP、低延迟预览、AJB、ABR、零拷贝上的经验迁移到 WebRTC。

## 后续可扩展

下一步建议进入 Phase 1，实现 Node.js WebSocket 信令服务和浏览器 WebRTC 通话 MVP，并同步生成 README、架构文档、建连流程面试笔记和过程日志。
