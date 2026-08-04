# 014 H264 Bitstream Probe

## 本次目标

继续 Phase 3 的 Encode / RTP / WebRTC 边界实验，在 `ffmpeg_probe` 中加入 H.264 Annex-B 导出和 NALU type 统计，为后续 H264 over RTP packetization 做准备。

## 做了什么

修改 `native/ffmpeg_probe`：

1. 新增 `--annexb-output` 参数。
2. 使用 FFmpeg `h264_mp4toannexb` bitstream filter。
3. 将 MP4/AVCC H.264 转成 Annex-B H.264。
4. 扫描 Annex-B start code。
5. 统计 NALU type。
6. 修复 `BsfGuard` RAII move-only 语义，避免 AVBSFContext 被临时对象析构提前释放。

新增文档：

1. `docs/phase3/h264-bitstream-probe-design.md`
2. `docs/interview-notes/11-h264-annexb-nalu.md`

## 遇到的问题

第一次实现 bitstream filter 时出现段错误。原因是 `BsfGuard` 包含裸指针 `AVBSFContext*`，返回和赋值时发生浅拷贝，临时对象析构提前释放了 context。

## 怎么排查

先运行小样本，发现输出文件为空且进程 segmentation fault。结合 C++ RAII 对象生命周期判断，问题不在 FFmpeg filter 本身，而在 guard 对象的拷贝语义。

## 怎么解决

将 `BsfGuard` 改为 move-only：禁止拷贝，支持 move constructor 和 move assignment。这样 `AVBSFContext` 的所有权只会移动，不会被临时对象提前释放。

## 验证结果

使用统一素材：

```bash
/home/bfm01000/workspace/video_downloads/FRXXZ.mp4
```

命令：

```bash
./build/ffmpeg_probe/ffmpeg_probe \
  --input /home/bfm01000/workspace/video_downloads/FRXXZ.mp4 \
  --packets 80 \
  --annexb-output ../captures/frxxz-first80.h264
```

结果：

```text
extradata : 49 bytes (looks like AVCC)
total_nalus : 163
type 1 (non-IDR slice) : 79
type 5 (IDR slice) : 1
type 6 (SEI) : 1
type 7 (SPS) : 1
type 8 (PPS) : 1
type 9 (AUD) : 80
```

输出文件：

```text
captures/frxxz-first80.h264, 129KB
```

## 技术取舍

当前只统计 NALU type，没有解析 SPS/PPS 内容，也没有做 RTP packetization。这样能先把 AVCC -> Annex-B -> NALU 这个边界打通，下一步再进入 FU-A 分片和 RTP timestamp / sequence 模拟。

## 和我过往经验的连接

这一步连接 H.264、RTMP 和 WebRTC。RTMP 推流经常要处理 SPS/PPS、关键帧和首帧可解码；WebRTC 中也有 IDR、PLI/FIR、RTP H264 packetization 和弱网恢复问题。

## 面试讲法

我在 FFmpeg Probe 的基础上继续扩展了 H.264 bitstream 分析。MP4 里的 H.264 通常是 AVCC，我用 h264_mp4toannexb 转成 Annex-B，然后扫描 start code 统计 NALU 类型。这样可以讲清楚 SPS/PPS/IDR/non-IDR/AUD 的作用，也能自然过渡到 H264 over RTP：小 NALU 可以单包发送，大 NALU 要 FU-A 分片，弱网丢包后恢复依赖 IDR 和参数集。

## 后续可扩展

1. 实现 H264 FU-A packetization 实验。
2. 模拟 RTP sequence number 和 timestamp。
3. 解析 SPS/PPS。
4. 输出 NALU JSON 报告。
