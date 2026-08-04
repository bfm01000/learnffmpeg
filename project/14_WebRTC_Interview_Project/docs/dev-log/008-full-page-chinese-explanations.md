# 008 全页面中文解释

## 本次目标

把 WebRTC 实验页面从“能操作、能看指标”升级为“能边看边讲解”的面试展示页面。用户希望整个页面都有中文注释，帮助理解每个区域、控件和指标的含义。

## 做了什么

重写 `public/index.html` 的文案层：

1. 顶部增加“这个页面用来验证什么”。
2. 控制区增加中文说明，解释 Room、Source、Resolution、FPS、Max Bitrate 的作用。
3. 视频区增加说明，解释 Local 和 Remote 分别对应采集/发送源与接收/解码渲染。
4. 实时指标区增加说明，解释 RTT、Jitter、Packet Loss、Send Bitrate、Recv Bitrate、FPS、Resolution、ICE、Data RTT、Target Bitrate。
5. 趋势图区补充中文说明，解释 target/send/recv 和 rtt/jitter 的关系。
6. 事件日志区增加排查顺序说明，帮助定位 offer、answer、candidate、PeerConnection 状态问题。

修改 `public/styles.css`：

1. 新增 `.explain-panel` 和 `.section-head` 样式。
2. 新增 `label small` 样式，让控件解释跟控件绑定。
3. 新增 `.stat em` 样式，让指标卡片有中文释义。
4. 新增 `.panel-title span` 样式，让 Local/Remote、图表和日志标题有中文副标题。

## 遇到的问题

原页面已经有指标和图表，但对不熟悉 WebRTC stats 的人来说，看到 RTT、jitter、target、send、recv 并不一定能马上理解它们对应链路中的哪个位置。面试展示时，如果每个指标都要临时口头解释，容易漏掉关键点。

## 怎么排查

按页面结构拆分信息层：顶部目标、控制区、视频区、指标区、趋势图区、事件日志区。每一块只补和当前区域直接相关的说明，避免把页面写成大段文档。

## 怎么解决

使用普通 HTML 文案承载解释，而不是把说明写进 canvas 或 JS。这样说明可读、可复制、可维护，也不会影响 WebRTC 逻辑和 stats 采样逻辑。

## 技术取舍

说明文案尽量短，偏工程解释，不做教程式长篇文本。页面仍然保留英文指标名，是为了对应浏览器 WebRTC stats 和面试中的标准术语；中文解释负责说明含义和排查价值。

## 涉及的关键知识点

1. WebRTC 建连链路解释。
2. 采集源与测试源隔离。
3. 编码参数控制和目标码率。
4. RTT、jitter、loss、bitrate、fps、resolution、ICE state。
5. DataChannel RTT。
6. 事件日志辅助建连排查。

## 和我过往经验的连接

这一步对应“工程展示能力”。做音视频优化不只是把功能做出来，还要能把指标和链路位置讲清楚。之前做 RTMP、低延迟预览、AJB 和 ABR 时，也需要把队列水位、码率、丢帧、延迟这些指标讲给别人听。现在页面直接提供中文解释，有助于把项目变成可面试表达的作品。

## 面试讲法

我把实验台页面做成了可解释的结构：控制区对应采集和编码输入，视频区对应本地采集与远端渲染，指标区对应 WebRTC stats 的关键观测点，趋势图区用于观察控制量和结果量的变化，事件日志区用于排查建连流程。这样面试官不需要先读代码，就能从页面上看到我如何拆解实时音视频链路。

## 后续可扩展

1. 给每个指标增加 hover tooltip 或“常见异常解释”。
2. 增加一键弱网场景说明。
3. 增加自动 ABR 策略说明和当前策略状态。
4. 增加 stats JSON 导出后的离线分析说明。
