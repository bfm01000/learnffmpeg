# 11 H264 Annex-B 和 NALU

## 面试官可能怎么问

1. MP4 里的 H.264 和 Annex-B 有什么区别？
2. SPS/PPS/IDR 分别是什么？
3. H.264 over RTP 怎么分包？
4. WebRTC 中 PLI/FIR 为什么和关键帧有关？

## 一句话回答

MP4 里的 H.264 通常是 AVCC 格式，使用长度字段描述 NALU；Annex-B 使用 start code 分隔 NALU。WebRTC/RTP 传输 H.264 时要按 NALU 分包，IDR 是关键恢复点，SPS/PPS 提供解码参数。

## 项目里怎么体现

`ffmpeg_probe` 支持 `--annexb-output`，用 FFmpeg `h264_mp4toannexb` 将统一素材 `FRXXZ.mp4` 的 H.264 转为 Annex-B，并统计 NALU type。

## 关键概念

1. SPS：序列参数集，描述 profile、level、分辨率等。
2. PPS：图像参数集，描述 entropy coding、slice group 等图像级参数。
3. IDR：即时解码刷新帧，解码器可以从这里恢复，不依赖之前参考帧。
4. non-IDR：普通预测帧，通常依赖参考帧。
5. AUD：访问单元分隔符。
6. SEI：补充增强信息。

## 和我过往经验的连接

这一步连接 H.264 编码知识、RTMP 推流和 WebRTC 传输。以前做 RTMP 时也会关心 SPS/PPS、关键帧间隔和首帧可解码；WebRTC 中同样需要关注 IDR、PLI/FIR 和弱网后的恢复速度。

## 常见坑

1. 把 AVCC 和 Annex-B 混用，导致解码器不认数据。
2. 发送 IDR 但没有携带 SPS/PPS，接收端无法初始化解码器。
3. 大 NALU 没有做 FU-A 分片，超过 MTU。
4. GOP 太长导致丢包后恢复慢。

## 可继续深入

1. 实现 H264 FU-A packetization。
2. 模拟 RTP sequence number 和 timestamp。
3. 分析 SPS/PPS 内容。
4. 对比 WebRTC PLI/FIR 和 RTMP 关键帧请求/重推策略。
