# 10 FFmpeg 时间戳和 GOP

## 面试官可能怎么问

1. PTS 和 DTS 有什么区别？
2. time_base 是什么？
3. GOP 和关键帧对实时传输有什么影响？
4. FFmpeg 的 packet 时间戳和 WebRTC RTP timestamp 有什么关系？

## 一句话回答

PTS 是显示时间，DTS 是解码时间，time_base 是时间戳单位；GOP/keyframe 决定随机访问和错误恢复能力。WebRTC 虽然使用 RTP timestamp，但底层仍然需要把采集、编码、发送和播放时间映射清楚。

## 项目里怎么体现

`native/ffmpeg_probe` 打印媒体文件的 stream 信息，并逐个 video packet 输出 PTS、DTS、duration、size、keyframe 和 GOP 间隔。

## 底层原理

FFmpeg demux 出来的 AVPacket 是压缩数据包。packet 的 PTS/DTS 还处在 stream time_base 下，转换成秒要使用：

```text
seconds = timestamp * av_q2d(stream->time_base)
```

如果视频存在 B 帧，显示顺序和解码顺序不同，PTS 和 DTS 就可能不同。实时系统里这意味着额外 reorder buffer 和延迟。

## 和我过往经验的连接

这和你之前做精准 Seek、frame index、int64 pts、Smart Seek 是同一个知识体系。WebRTC 的 jitter buffer、关键帧请求、首帧渲染和卡顿恢复，也都离不开时间戳和关键帧理解。

## 常见坑

1. 直接把 PTS 当毫秒使用，忘记 time_base。
2. 忽略 B 帧导致的 PTS/DTS 差异。
3. 只看帧率，不看 GOP 和 keyframe 间隔。
4. H264 Annex-B / AVCC 格式混淆。

## 可继续深入

1. 解析 H264 NALU。
2. 对比固定 GOP 和开放 GOP。
3. 统计 B 帧数量和 reorder 深度。
4. 连接到 RTP timestamp 和 WebRTC jitter buffer。
