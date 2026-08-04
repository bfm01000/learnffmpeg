# 006 编码参数控制

## 本次目标

继续推进 Phase 2，加入可操作的编码参数控制，让用户能动态调整发送端 `maxBitrate`，并结合 stats 趋势图观察真实码率变化。

## 做了什么

修改 `public/index.html`：

1. 新增 `Max Bitrate` 输入框。
2. 新增 `Apply Encoding` 按钮。
3. 新增 `Target Bitrate` 指标卡片。

修改 `public/app.js`：

1. 增加 `getVideoSender()`，从 PeerConnection 中找到 video sender。
2. 增加 `applyEncodingParameters()`，调用 `RTCRtpSender.getParameters()` / `setParameters()`。
3. 设置 `encodings[0].maxBitrate` 和 `encodings[0].maxFramerate`。
4. Join 后自动应用一次编码参数。
5. 点击 `Apply Encoding` 后可以运行中调整码率。
6. 在 bitrate 趋势图中加入 target 曲线，和 send / recv 曲线对比。

新增 `docs/interview-notes/08-encoding-parameters-and-abr.md`。

## 遇到的问题

WebRTC 的 `maxBitrate` 不是“真实码率等于目标码率”。真实发送码率会被内容复杂度、分辨率、帧率、编码器、浏览器 pacing、网络估计和拥塞控制共同影响。如果只显示输入框，不显示 target / send / recv 的对比，就很难解释为什么设置值和实际值不同。

## 怎么排查

沿用已有 stats dashboard，把控制量和观测量放在一起：`Target Bitrate` 表示人为设置的上限，`Send Bitrate` 和 `Recv Bitrate` 表示实际统计结果。通过趋势图观察修改前后的变化，而不是只看按钮是否执行成功。

## 怎么解决

通过 `RTCRtpSender.setParameters()` 做无重协商的 sender encoding 调整，并把 target bitrate 也加入图表。这样可以直接观察“目标值、发送端实际值、接收端实际值”的差异。

## 技术取舍

当前只动态控制 `maxBitrate` 和 `maxFramerate`，没有运行中切换分辨率。分辨率切换更适合通过重新采集或 `replaceTrack()` 实现，复杂度更高，放到下一步更合适。

也没有直接实现自动 ABR，因为当前先需要手动控制和观测基线。等弱网模拟加入后，再基于 RTT / jitter / loss / bitrate 做简单策略。

## 涉及的关键知识点

1. `RTCRtpSender`。
2. `getParameters()` / `setParameters()`。
3. `encodings[0].maxBitrate`。
4. `encodings[0].maxFramerate`。
5. 目标码率和实际码率的区别。
6. WebRTC 拥塞控制和手动码率限制的关系。

## 和我过往经验的连接

这一步和之前做 RTMP 推流 ABR 很接近：不是只设置一个码率，而是要结合网络状态、队列水位、掉帧和实际发送表现做反馈。WebRTC 中我们先用 `setParameters()` 作为控制入口，用 stats 作为反馈入口，后续可以自然扩展为简单 ABR。

## 面试讲法

我在 stats 面板之后继续加了编码参数控制。页面可以动态设置 video sender 的 `maxBitrate` 和 `maxFramerate`，底层通过 `RTCRtpSender.setParameters()` 实现，不需要重新 SDP 协商。同时我把 target bitrate、send bitrate、recv bitrate 画在同一张趋势图里，因为 WebRTC 里的目标码率不是实际码率，真实值还受到内容复杂度、编码器和拥塞控制影响。这个设计能把我之前做 RTMP ABR 的经验迁移过来：先有控制入口，再有观测反馈，最后才能做自动策略。

## 后续可扩展

1. 增加码率 preset 按钮。
2. 增加运行中分辨率切换和 `replaceTrack()`。
3. 增加基于 stats 的简单自动 ABR。
4. 增加弱网模拟，验证手动码率限制和 WebRTC 自身 BWE 的关系。
