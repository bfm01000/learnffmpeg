# FFmpeg Timestamp / GOP Probe 设计说明

## 目标

`native/ffmpeg_probe` 用 C++ 调用 FFmpeg libavformat，读取媒体文件并打印 stream 信息、packet PTS/DTS/time_base/keyframe，同时统计 GOP 间隔。

## 分析流程

```text
avformat_open_input
  -> avformat_find_stream_info
  -> print format / stream info
  -> find first video stream
  -> av_read_frame loop
  -> print pts / dts / duration / keyframe
  -> count keyframe interval as GOP packet distance
```

## 为什么关注 PTS / DTS / time_base

实时音视频链路里，时间模型是核心。PTS 决定呈现时间，DTS 决定解码顺序，time_base 决定时间戳单位。存在 B 帧时，PTS 和 DTS 可能不同，这会影响解码重排、延迟和精确 Seek。

WebRTC 中虽然最终使用 RTP timestamp，但采集时间、编码时间、RTP timestamp、jitter buffer 播放时间之间同样需要清晰映射。

## 为什么关注 GOP / keyframe

关键帧决定随机访问、错误恢复和 WebRTC 中 PLI/FIR 请求后的恢复速度。GOP 越长，压缩效率通常更高，但错误恢复和首帧等待可能更慢；GOP 越短，恢复更快但码率开销更大。

## 当前支持

1. 打印 container format、duration、bitrate。
2. 打印每个 stream 的 codec、time_base、分辨率、fps。
3. 遍历前 N 个 video packet。
4. 打印 PTS、DTS、duration、packet size、keyframe。
5. 统计 keyframe interval。

## 后续扩展

1. 解析 H264 Annex-B / AVCC extradata。
2. 打印 NALU type。
3. 统计 B 帧导致的 PTS/DTS reorder。
4. 输出 CSV/JSON，和 WebRTC stats 做对比分析。
