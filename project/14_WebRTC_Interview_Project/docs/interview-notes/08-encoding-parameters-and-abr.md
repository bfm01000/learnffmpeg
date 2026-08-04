# 08 编码参数控制和 ABR

## 面试官可能怎么问

1. WebRTC 里怎么限制发送码率？
2. `maxBitrate` 和真实发送码率是什么关系？
3. WebRTC 的拥塞控制和直播 ABR 有什么不同？
4. 你怎么证明调参真的生效？

## 一句话回答

浏览器 WebRTC 可以通过 `RTCRtpSender.getParameters()` / `setParameters()` 修改 sender encoding，例如设置 `maxBitrate` 和 `maxFramerate`；真实发送码率还会受到内容复杂度、分辨率、帧率、浏览器编码器和 WebRTC 拥塞控制共同影响，所以必须结合 stats 趋势观察。

## 项目里怎么体现

页面新增 `Max Bitrate` 输入框和 `Apply Encoding` 按钮。连接后，点击按钮会找到 video sender，并设置：

1. `params.encodings[0].maxBitrate = bitrateKbps * 1000`。
2. `params.encodings[0].maxFramerate = fpsSelect.value`。

页面同时显示 `Target Bitrate`，并在 `Bitrate Trend` 图中绘制 target / send / recv 三条曲线，用来观察目标码率和真实发送/接收码率之间的关系。

## 底层原理

`RTCRtpSender` 表示本端向对端发送的一路 RTP sender。`setParameters()` 可以在不重新协商 SDP 的情况下调整部分发送参数。`maxBitrate` 是发送端编码/发送的上限约束，不是保证值。真实码率可能低于目标值，原因包括测试画面复杂度低、编码器输出不足、网络估计带宽不足、浏览器内部 pacing 和拥塞控制限制等。

WebRTC 的拥塞控制更偏互动实时：目标是在延迟、丢包和可用带宽之间快速平衡。传统 RTMP 直播 ABR 更常见的是根据队列水位、发送阻塞、丢帧和服务端/播放器反馈做档位或码率调整。

## 和我过往经验的连接

这一步可以直接连接之前做 4K RTMP 推流中的 ABR 和弱网掉帧经验。以前可能根据发送队列水位、网络阻塞和帧率稳定性动态调码率；在 WebRTC 里，可以先用 `setParameters()` 作为控制入口，再结合 getStats 的 bitrate、RTT、jitter、loss 观察效果。

## 常见坑和排查方式

1. 设置了 `maxBitrate` 但 send bitrate 没明显变化：检查画面复杂度是否太低，或目标码率是否高于当前实际需求。
2. send bitrate 降了但 recv bitrate 不同步：观察采样窗口、接收端 stats 和网络路径。
3. 设置太低导致画面糊或 fps 降低：说明编码预算不足。
4. 改分辨率通常需要替换 track 或重新采集，不是简单改 `setParameters()` 就能完成。
5. 不同浏览器对 encoding parameters 支持细节可能不同，要用日志和异常处理兜底。

## 可继续深入方向

1. 增加一键档位：200kbps / 800kbps / 2Mbps。
2. 增加基于 stats 的简单 ABR：loss 或 RTT 上升时自动降码率。
3. 增加 `replaceTrack()`，支持运行中切换分辨率。
4. 增加弱网模拟，观察 WebRTC 自身 BWE 和手动码率限制的交互。

## Simple ABR 策略

当前项目加入了一个教学版 Simple ABR：

1. 如果 packet loss 增量较大，或 RTT 超过 350ms，或 jitter 超过 80ms，就把目标码率降到当前的 70%，最低不低于 150kbps。
2. 如果连续 8 个采样周期都比较稳定，就把目标码率提高到当前的 115%，最高不超过 2500kbps。
3. 每次调整都会通过 `RTCRtpSender.setParameters()` 重新设置 `maxBitrate`。
4. 页面用 `ABR State` 展示当前是 Manual、观察中、降码率、升码率还是稳定观察。

这个策略不是生产级 GCC/BWE 替代品，而是为了演示“控制入口 + 观测反馈 + 策略决策”的闭环。生产系统还需要考虑带宽估计、码率平滑、分辨率档位、帧率、关键帧、编码器反馈、队列水位和用户体验指标。

面试时可以强调：WebRTC 内部本身有拥塞控制，本项目的 Simple ABR 是应用层策略实验，用来帮助理解 ABR 决策过程，而不是要覆盖浏览器内置 BWE。
