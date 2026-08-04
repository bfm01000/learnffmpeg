# 面试讲解：H.264 RTP Packetizer

## 一句话说明

我在 native 侧实现了一个离线 H.264 RTP packetizer，用来验证 H.264 Annex-B 裸流如何映射成 RTP 包，重点覆盖 single NALU 和 FU-A 分片。

## 可以这样讲项目链路

浏览器 WebRTC MVP 验证的是 PeerConnection、DataChannel 和 stats；native 阶段我往底层媒体链路推进了一层：先用 FFmpeg 把 MP4 中的 H.264 从 AVCC 转成 Annex-B，再解析 NALU，最后模拟 H.264 over RTP 的打包过程。

这个过程让我可以解释 WebRTC 发送端真正关心的几个问题：

- SPS/PPS 负责告诉解码器视频参数。
- IDR 是接收端可以重新开始解码的关键帧。
- 普通 slice 是预测帧数据。
- RTP sequence number 用来检测丢包和恢复顺序。
- RTP timestamp 表示媒体采样时间，同一帧的多个 RTP 包 timestamp 相同。
- marker bit 标记一帧的最后一个 RTP 包。
- 大 NALU 不能直接塞进一个 RTP 包，需要用 FU-A 分片。

## 面试展开点

### 为什么 MP4 里的 H.264 不能直接发

MP4 里 H.264 通常是 AVCC 格式，每个 NALU 前面是长度字段；而 RTP/H.264 常见处理链路更接近 Annex-B/NALU 的视角。因此我先用 FFmpeg bitstream filter 做 `h264_mp4toannexb`，把编码数据转换成便于解析和打包的格式。

### FU-A 是怎么做的

当一个 NALU 大于 MTU 或设定的 RTP payload 上限时，需要拆成多个 RTP 包。FU-A 会把原始 NALU header 拆成 FU indicator 和 FU header：

- 第一个分片设置 Start bit。
- 最后一个分片设置 End bit。
- 中间分片只携带连续 payload。
- 所有分片保留同一个 RTP timestamp。
- 只有最后一个分片可能设置 marker bit。

### 这一步和 libwebrtc 的关系

真正接入 libwebrtc 时，不一定需要自己手写 RTP packetizer，因为 libwebrtc 内部已有 RTP packetization 和 pacing 机制。但我实现这个离线 demo 的价值是：我能说清楚 libwebrtc 在内部帮我做了什么，以及为什么编码帧、时间戳、关键帧、SPS/PPS 这些输入信息很重要。

## 已验证现象

使用统一素材 `FRXXZ.mp4` 生成前 80 个视频 packet 的 Annex-B 裸流后，packetizer 结果显示：

- 80 帧
- 163 个 NALU
- 159 个 RTP 包
- 92 个 FU-A 包
- 67 个 single NALU 包

第一帧包含 SPS/PPS/SEI/IDR，其中 IDR 因为体积较大被切成多个 FU-A 包，非常适合演示关键帧分片。
