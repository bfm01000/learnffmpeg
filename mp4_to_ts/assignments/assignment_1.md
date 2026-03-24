# 作业 1：av_read_frame() 如何读取数据包

## 目标

阅读 FFmpeg 源码 `libavformat/utils.c` 中的 `av_read_frame()`，理解其如何通过 demuxer 读取一帧数据并封装为 `AVPacket`。

---

## 要求

1. **定位源码**  
   在 FFmpeg 仓库中找到 `av_read_frame()` 的实现（在 `libavformat/utils.c` 中），并注明你阅读的 FFmpeg 版本或 commit（若适用）。

2. **简述调用链**  
   用文字或流程图说明：从调用 `av_read_frame()` 开始，到返回一个 `AVPacket`，中间经过了哪些关键步骤（例如：如何找到 demuxer、调用了 demuxer 的哪个接口、如何填充 AVPacket 等）。

3. **与封装格式的关系**  
   简要说明：对于 **MP4** 和 **TS** 两种格式，demuxer 分别是在哪一层、利用哪些结构（如 moov/trak/mdat 或 PAT/PMT/PES）来产生 AVPacket 的。

---

## 作答区

（在下方填写你的答案）

### 1. 源码位置与版本

- 文件路径：
- FFmpeg 版本/commit（如有）：

### 2. 调用链简述

（文字或流程图）

### 3. 与 MP4/TS 的关系

- MP4：
- TS：

---

## 验证标准

- 能准确写出 `av_read_frame()` 所在文件及主要逻辑。
- 能说明“格式无关 API → demuxer 读包 → 填充 AVPacket”的流程。
- 能正确关联 MP4 的 moov/trak/mdat 与 TS 的 PAT/PMT/PES 在解析过程中的作用。

---

## 参考答案（含讲解）

> 以下为示例答案。不同 FFmpeg 版本在函数细节上可能有差异，但整体流程一致。

### 1. 源码位置与版本（示例）

- 文件路径：`libavformat/utils.c`
- 关注函数：`av_read_frame(AVFormatContext *s, AVPacket *pkt)`
- FFmpeg 版本/commit：可按你本地实际版本填写（如 `ffmpeg 7.x`）

### 2. 调用链简述（示例）

**简化流程：**

1. 业务代码调用 `av_read_frame(s, pkt)`。
2. `av_read_frame` 位于 `libavformat/utils.c`，它本身是“统一入口”，不关心具体是 MP4 还是 TS。
3. 函数内部会走到当前输入格式对应的 demuxer 读包逻辑（可理解为 `s->iformat` 下的读包回调链）。
4. demuxer 从输入流读取并解析一个可输出的数据单元，写入 `AVPacket`（`data`、`size`、`stream_index`、`pts/dts` 等）。
5. `av_read_frame` 返回 0 表示成功，调用方得到一个可送解码器的数据包；若到文件尾或失败，返回负值错误码。

**讲解：**

- `av_read_frame()` 的核心价值是把“不同封装格式的读包差异”统一到一个 API。  
- 真正知道 MP4 box 或 TS 包结构的是 demuxer，而不是 `av_read_frame` 本身。  
- 你可把它理解成“调度层 + 通用封装层”。

### 3. 与 MP4/TS 的关系（示例）

- **MP4：**  
  在 `avformat_open_input` 阶段，mov demuxer 先读 `moov` 建立轨道和索引（`trak` 信息会映射到 `AVStream`）。后续 `av_read_frame` 调 mov 读包时，会依据这些索引去 `mdat` 中取对应媒体数据，组装成 `AVPacket` 返回。

- **TS：**  
  mpegts demuxer 按 188 字节包读取，先依赖 PAT/PMT 建立“PID → 流类型/流对象”映射，再从对应 PID 重组 PES，提取时间戳与 payload，最终输出为 `AVPacket`。

**讲解：**

- MP4 是“先元数据索引（moov/trak）后取数据（mdat）”。
- TS 是“边读包边根据 PID 归类并重组数据”。
- 两者封装思想不同，但都通过 demuxer 最终产出统一的 `AVPacket`，这也是 FFmpeg 抽象层设计的关键。
