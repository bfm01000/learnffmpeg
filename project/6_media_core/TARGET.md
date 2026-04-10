你是一位资深 C++ 多媒体架构师。请基于 FFmpeg 在我的仓库 `project` 目录下设计并实现一个“可复用的编解码模块（codec core）”，要求后续可直接复用于 WebRTC 工程。并提供使用转码的使用demo

【目标】
1) 实现可复用的解码器/编码器抽象层，支持视频转码主链路。
2) 支持指定输出封装格式和编码参数（如 mp4/flv/mkv，H264/H265/VP8/VP9/AV1 等可扩展）。
3) 支持硬件解码（优先：VideoToolbox/NVDEC/QSV/VAAPI，按平台自动选择，可回退软解）。
4) 具备良好扩展性：后续能接入 WebRTC 的采集、编码、RTP 打包发送链路，尽量复用同一套编解码逻辑。
5) 代码结构清晰、模块边界明确、错误处理统一、便于单元测试和集成测试。

【技术约束】
- 语言：C++11/14（优先保持与现有工程兼容）
- 构建：CMake
- 多媒体库：FFmpeg（libavformat/libavcodec/libavutil/libswscale）
- 先实现“视频优先”，音频接口预留扩展点
- 尽量避免业务逻辑耦合 FFmpeg 原始 API 到处散落，做一层适配封装

【请输出内容】
A. 先给出总体架构设计（文字 + 模块职责）
- 建议目录结构（放在 `project/media_core` 或你认为更合理的位置）
- 关键模块划分（示例）：
  - demuxer
  - decoder (soft/hard)
  - frame_converter (pixel/sample format, scale)
  - encoder
  - muxer
  - pipeline/orchestrator
  - config & capability probe
  - common error/log/timestamp utils

B. 给出核心接口定义（头文件级别）
- IVideoDecoder / IVideoEncoder / ICodecFactory / IFrameConverter / IMuxer 等
- 统一数据结构：Packet、Frame、CodecConfig、StreamInfo、Timebase
- 统一错误码体系与日志接口
- 生命周期与线程模型说明（init/start/stop/flush/reset）

C. 给出“硬解码能力探测 + 回退策略”
- 启动时如何探测硬件能力
- 解码器选择优先级（平台差异）
- 失败时如何自动回退软解并保留可观测日志

D. 给出“可复用到 WebRTC”的设计约束
- 哪些模块必须与传输层解耦
- 如何把编码输出对接 WebRTC（如 AnnexB/AVCC、关键帧请求、时间戳）
- 需要预留的接口（码率控制、分辨率切换、帧率调整、IDR 强制请求）

E. 实施计划（分阶段）
- Phase1：最小可运行（本地文件转码）
- Phase2：硬解接入 + 能力探测
- Phase3：WebRTC 复用改造（接口适配）
每阶段给出可验证的验收标准。

F. 直接给出首批可落地代码骨架
- 至少包含：
  - 目录和文件清单
  - 关键头文件与最小实现桩
  - CMakeLists 修改建议
  - 一个 demo main（读本地视频->解码->编码->写输出）
- 对关键设计决策写简短注释（为什么这么做）

【代码质量要求】
- RAII 管理 FFmpeg 资源，避免泄漏
- 严格处理 flush（decoder/encoder/muxer）
- 时间戳换算清晰、可审计
- 错误路径可回收，避免中途 return 造成资源泄漏
- 不要只给概念，必须给可编译的骨架代码与明确下一步 TODO

请按“先设计评审，再代码骨架，再实施步骤”的顺序输出。