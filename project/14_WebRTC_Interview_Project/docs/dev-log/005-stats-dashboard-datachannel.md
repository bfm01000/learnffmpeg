# 005 Stats Dashboard 和 DataChannel 延迟

## 本次目标

把 Phase 1 的瞬时 stats 面板升级成 Phase 2 的可观测实验台雏形：增加趋势图，并加入 DataChannel ping-pong 延迟测量。

## 做了什么

修改 `public/index.html`：

1. 增加 `Data RTT` 指标卡片。
2. 增加 `Bitrate Trend` 和 `Latency Trend` 两个 canvas 图表。

修改 `public/app.js`：

1. offer 端创建 `latency` DataChannel。
2. answer 端通过 `ondatachannel` 接收 DataChannel。
3. 每秒发送 ping，对端收到后返回 pong，本端计算 Data RTT。
4. 每秒读取 `getStats()` 后保存最近 60 个采样点。
5. 用 canvas 绘制 send / recv bitrate 趋势，以及 RTT / jitter 趋势。

修改 `README.md`，补充 DataChannel ping-pong 和趋势图能力。

新增 `docs/interview-notes/07-stats-and-datachannel-latency.md`，沉淀面试讲法。

## 遇到的问题

只看瞬时 stats 很难讲清楚链路变化。例如弱网出现时，码率下降、jitter 上升、RTT 波动是一个过程，不是一帧静态数据。另一方面，candidate-pair RTT 偏网络路径，不能完全代表 JS 应用层消息往返耗时。

## 怎么排查

先保留已有 `getStats()` 逻辑，再区分两类指标：媒体/网络 stats 来自 WebRTC 栈，应用层往返耗时用 DataChannel ping-pong 补充。这样不混淆二者含义。

## 怎么解决

用无依赖 canvas 图表保存最近 60 秒采样，避免引入图表库。用 DataChannel 做轻量 ping-pong，避免影响媒体链路太多，同时能验证控制通道可用性。

## 技术取舍

当前趋势图只做最小可读版本，没有引入 ECharts / Chart.js。这样项目仍保持轻量、可解释。后续如果需要更强交互，再考虑引入图表库或保存 stats JSON 做离线分析。

DataChannel RTT 不是视频端到端延迟，它测的是应用层消息往返。真正的视频 E2E 延迟后续还需要画面时间戳或 sender / receiver 时间同步方案。

## 涉及的关键知识点

1. `RTCPeerConnection.getStats()`。
2. candidate-pair RTT。
3. RTP jitter 和 packet loss。
4. send / recv bitrate 计算。
5. DataChannel。
6. SCTP over DTLS。
7. stats 采样和趋势观察。

## 和我过往经验的连接

这一步对应以前做低延迟预览、RTMP 推流和 ABR 时的全链路埋点。实时音视频优化不能只看画面主观感受，要把网络、队列、码率、帧率和延迟变成指标。WebRTC 的 stats 面板就是这个项目后续做弱网、降码率、降帧和 jitter buffer 分析的基础。

## 面试讲法

我在 MVP 跑通后，没有只停留在能通话，而是立刻补了可观测能力。页面每秒读取 getStats，展示并绘制 RTT、jitter、丢包、发送/接收码率等趋势；同时用 DataChannel 做 ping-pong，测应用层消息往返延迟。这样后面做弱网或码率控制时，我能讲清楚指标怎么变化、为什么变化，以及如何根据指标定位问题。

## 后续可扩展

1. 增加 stats JSON 导出。
2. 增加编码参数动态控制。
3. 增加画面时间戳端到端延迟测量。
4. 增加弱网模拟和策略对比。
