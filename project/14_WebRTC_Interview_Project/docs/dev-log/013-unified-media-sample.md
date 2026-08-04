# 013 统一视频素材

## 本次目标

用户指定后续所有需要视频素材的地方统一使用 `/home/bfm01000/workspace/video_downloads/FRXXZ.mp4`。本次将该约定写入项目规则，并用该素材重新验证 `ffmpeg_probe`。

## 做了什么

1. 检查 `FRXXZ.mp4` 是否存在并可读取。
2. 用 `ffmpeg_probe` 分析该素材的前 40 个 video packet。
3. 在 `AGENTS.md` 增加“统一素材约定”。
4. 在 `native/README.md` 增加默认素材路径和 probe 命令。

## 验证结果

素材路径：

```text
/home/bfm01000/workspace/video_downloads/FRXXZ.mp4
```

文件信息：

```text
size: 121MB
container: MP4 Base Media
```

`ffmpeg_probe` 关键输出：

```text
format    : QuickTime / MOV
duration  : 1203.221s
bitrate   : 839002
streams   : 2
video     : h264, 852x480, fps 25.001, time_base 1/16000
audio     : aac, 48000Hz, 2 channels
```

前 40 个 video packet 中，PTS 和 DTS 明显不同，并且 packet 输出顺序不是按 PTS 单调递增。这说明该素材包含帧重排序，适合用于讲解 B 帧、PTS/DTS 区别、解码顺序和显示顺序。

## 遇到的问题

之前验证 `ffmpeg_probe` 使用的是旧项目里的 `test_short_gop.mp4`。现在用户明确要求统一素材，因此后续不能再随意换素材，否则文档和面试讲法会分散。

## 怎么解决

将统一素材路径写入 `AGENTS.md`，后续所有需要视频素材的实验默认使用 `FRXXZ.mp4`。如果某个实验必须使用特殊素材，需要在对应 dev-log 中说明原因。

## 面试讲法

我给 native 阶段固定了一个统一视频素材，这样后续 FFmpeg、GOP、PTS/DTS、RTP packetization 和 WebRTC 相关实验都基于同一个样本分析。这个素材有 H.264 视频和 AAC 音频，且 PTS/DTS 存在明显重排序，很适合讲 B 帧、显示顺序、解码顺序和实时传输延迟之间的关系。

## 后续可扩展

1. 用该素材继续做 H264 NALU / Annex-B / AVCC 分析。
2. 统计 PTS/DTS reorder 深度。
3. 抽取关键片段生成短样本，但必须记录它来源于 `FRXXZ.mp4`。
4. 用该素材做后续 RTP packetization 实验。
