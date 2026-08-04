# 009 Simple ABR 策略

## 本次目标

在手动编码参数控制基础上，加入一个教学版 Simple ABR，让项目形成“采样 stats -> 判断网络状态 -> 调整 maxBitrate -> 观察趋势变化”的闭环。

## 做了什么

修改 `public/index.html`：

1. 控制区新增 `Strategy` 下拉框，支持 `Manual` 和 `Simple ABR`。
2. 实时指标区新增 `ABR State` 卡片，用来显示当前策略状态。

修改 `public/app.js`：

1. 新增 `evaluateSimpleAbr(sample)`。
2. 每次 `getStats()` 采样后调用 ABR 评估。
3. 当 packet loss 增量、RTT、jitter 超过阈值时，自动降低目标码率。
4. 当连续多个采样周期稳定时，缓慢提高目标码率。
5. 自动调整通过已有 `RTCRtpSender.setParameters()` 完成。
6. 策略切换时重置 ABR 状态，避免旧状态影响新实验。

更新 `README.md` 和 `docs/interview-notes/08-encoding-parameters-and-abr.md`。

## 遇到的问题

WebRTC 浏览器内部已经有自己的拥塞控制和带宽估计。如果项目再做一个应用层 ABR，很容易让人误解为“替代 WebRTC GCC”。因此需要明确：当前 Simple ABR 是教学和实验策略，用来展示反馈闭环，不是生产级拥塞控制实现。

## 怎么排查

先复用已有 stats 采样数据，确认能拿到 RTT、jitter、packet loss、send bitrate 和 target bitrate。然后把策略状态作为页面指标显示出来，避免自动调码率时用户不知道为什么变化。

## 怎么解决

采用非常保守的规则：

1. loss 增量 >= 3，或 RTT >= 350ms，或 jitter >= 80ms 时降码率。
2. 连续 8 个稳定采样周期后升码率。
3. 降码率乘以 0.7，升码率乘以 1.15。
4. 设置最小 150kbps、最大 2500kbps，避免过度震荡。

## 技术取舍

没有直接做复杂 GCC/BWE，也没有引入分辨率档位切换。当前只控制 `maxBitrate`，因为它最容易和已有趋势图形成闭环。后续可以继续加入降分辨率、降帧和队列水位策略。

## 涉及的关键知识点

1. ABR 反馈闭环。
2. RTT、jitter、packet loss 的策略意义。
3. `RTCRtpSender.setParameters()` 动态调码率。
4. WebRTC 内置拥塞控制与应用层策略的边界。
5. 码率上限、真实发送码率、接收码率之间的关系。

## 和我过往经验的连接

这一步和之前做 RTMP 推流 ABR 的经验最直接。RTMP 场景中可能根据发送队列水位、网络阻塞、掉帧和目标帧率调节码率；WebRTC 场景中可以根据 stats 暴露的 RTT、jitter、loss 和 bitrate 做应用层策略实验。两者共同点是：先观测，再决策，再验证效果。

## 面试讲法

我在手动码率控制之后做了一个 Simple ABR。它每秒读取 WebRTC stats，如果丢包、RTT 或 jitter 变差，就通过 setParameters 降低 maxBitrate；如果连续一段时间稳定，就缓慢升码率。页面会显示 ABR State，并在趋势图中同时展示 target、send、recv。这个设计不是为了替代 WebRTC 内部拥塞控制，而是为了把我过去做 RTMP ABR 的反馈闭环迁移到 WebRTC 实验中，帮助理解策略决策和指标变化的关系。

## 后续可扩展

1. 加入弱网模拟，真实触发降码率。
2. 加入分辨率档位和 `replaceTrack()`。
3. 加入降帧策略。
4. 记录每次 ABR 决策到 stats JSON 中，便于离线分析。
5. 对比浏览器内置 BWE 和应用层 Simple ABR 的交互。
