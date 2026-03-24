# 作业 5：C++ 代码实现 MP4 转 TS

## 目标

在不使用 ffmpeg 命令行的前提下，用 C++ 调用 FFmpeg API 完成 MP4 → TS 转换，并解释关键代码。

---

## 要求

1. **实现一个最小 remux 程序**
   - 输入：`input.mp4`
   - 输出：`output.ts`
   - 需包含以下关键步骤：
     - `avformat_open_input`
     - `avformat_find_stream_info`
     - `avformat_alloc_output_context2(..., "mpegts", ...)`
     - `avcodec_parameters_copy`
     - `av_read_frame`
     - `av_packet_rescale_ts`
     - `av_interleaved_write_frame`
     - `av_write_trailer`

2. **运行验证**
   - 程序成功生成 `output.ts`
   - 用 `ffprobe -hide_banner -show_streams output.ts` 验证输出流信息

3. **说明与讲解**
   - 解释为什么需要 `stream_map`（输入流索引到输出流索引映射）
   - 解释为什么必须做 `av_packet_rescale_ts`
   - 说明你是否处理了 H.264 的 `h264_mp4toannexb`（若没做，请说明限制）

---

## 作答区

### 1. 代码位置与编译命令

- 代码路径：
- 编译命令：
- 运行命令：

### 2. 关键代码片段

- 流映射部分：
- 读写包循环部分：
- 收尾释放部分：

### 3. 验证输出

- `ffprobe` 关键字段（codec_name / codec_type / duration 等）：
- 是否可正常播放：

### 4. 讲解与反思

- `stream_map` 的作用：
- `av_packet_rescale_ts` 的作用：
- 当前程序限制与下一步优化：

---

## 验证标准

- 程序可编译、可运行、能产出 TS 文件。
- 能正确解释时间戳重映射与多流映射。
- 有基本错误处理和资源释放，不卡死、不泄漏明显资源。

---

## 参考答案（含讲解）

> 这是“达标示例”的说明模板，与你的实际代码细节可不同，但流程应一致。

### 1. 实现思路（示例）

- 打开输入 MP4，读取流信息。
- 创建输出 mpegts 上下文，并为每个音视频流创建输出流。
- 把输入流参数复制到输出流（不重编码）。
- 循环读取 `AVPacket`，按 `stream_map` 改写输出流索引。
- 用 `av_packet_rescale_ts` 做时间戳转换后写入 TS。
- 写 trailer 并释放所有上下文。

**讲解：**

这就是最标准的 remux 管线。它与命令行 `-c copy -f mpegts` 的本质一致，只是你在代码里显式控制了每一步。

### 2. 关键问题回答（示例）

- **为什么要 `stream_map`？**  
  输入流索引与输出流索引通常不一致，且你可能会过滤掉非音视频流；没有映射会导致写包时索引错误。

- **为什么要 `av_packet_rescale_ts`？**  
  因为输入输出流的 `time_base` 可能不同。若不转换，容易出现时间戳异常（DTS/PTS 错乱、非单调）。

- **是否处理 `h264_mp4toannexb`？**  
  最小版可以先不做，但在部分素材上可能兼容性欠佳。生产代码建议对 H.264 视频包加 bitstream filter。

### 3. 达标输出特征（示例）

- `ffprobe` 能看到 `codec_type=video` 与 `codec_type=audio`。
- 输出容器为 TS，时长基本与输入一致。
- 输出文件可在常见播放器打开。

---

## 可选加分项

- 增加 `h264_mp4toannexb` bitstream filter。
- 支持仅转视频、仅转音频或按 stream specifier 过滤。
- 加入结构化日志，打印每 1000 个包的处理进度与时间戳范围。
