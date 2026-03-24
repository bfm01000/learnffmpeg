# Day 3：FFmpeg 解析流程与 av_read_frame()

## 1. 为什么要看 FFmpeg 源码

- 实际工程中解析 MP4/TS 通常不会从零写 box 或 188 字节包解析，而是使用 FFmpeg 的 `libavformat` 做 demux（解封装）。
- 理解 `av_read_frame()` 的调用链，能帮你把“封装格式概念”和“真实代码如何读出一帧”对应起来，并知道 **moov/trak/mdat** 或 **PAT/PMT/PES** 是在哪一层被用到的。

---

## 2. 入口：av_read_frame()

- **声明**：在 `libavformat/avio.h` 或通过 `libavformat/avformat.h` 暴露。
- **定义**：在 **`libavformat/utils.c`** 中实现。

作用：从已打开的 `AVFormatContext` 中**读出一个逻辑上的“一帧”数据**，封装成 `AVPacket` 返回。这里“一帧”对视频通常是一帧图像，对音频可能是一组采样，具体由 demuxer 决定。

### 2.1 调用关系（简化）

```
av_read_frame()
  → 在 utils.c 中实现
  → 内部会调用 s->iformat->read_packet 或类似 demuxer 的读接口
  → 即：当前格式对应的 demuxer（如 mov.c 对 MP4、mpegts.c 对 TS）的 read_packet
  → demuxer 从文件中读取并解析（会用到 moov/trak/mdat 或 PAT/PMT/PES）
  → 填充一个 AVPacket（含 data、size、stream_index、pts/dts 等）
  → 返回给调用者
```

因此：**av_read_frame() 是“格式无关”的 API，真正解析 MP4 的 moov/trak/mdat 或 TS 的包结构的是各个 demuxer（mov、mpegts 等）。**

---

## 3. libavformat/utils.c 中的相关逻辑

在 **`libavformat/utils.c`** 中建议重点看：

1. **av_read_frame() 的实现**
   - 如何根据 `AVFormatContext` 得到当前 demuxer。
   - 如何循环或单次调用 demuxer 的读接口（如 `read_packet`）。
   - 如何把读到的数据封装成 `AVPacket`，设置 `stream_index`、`pts`、`dts`、`duration` 等。
   - 是否有对 AVPacket 的缓冲、排队或时间戳修正。

2. **与 open/close 的衔接**
   - `avformat_open_input()` 会打开文件、探测格式、并调用 demuxer 的 `read_header`（如 mov 的 read_header 会解析 **moov** 和 **trak**，建立流索引）。
   - `av_read_frame()` 在 open 之后被多次调用，每次取一个 packet；对 MP4 来说，这些 packet 的数据最终来自 **mdat**，索引来自 **trak**。

3. **demuxer 的注册与匹配**
   - 如何根据文件内容或扩展名选择 mov 或 mpegts 等 demuxer（可结合 `avformat_open_input` 的探测逻辑看）。

---

## 4. 与 MP4/TS 的对应关系

- **MP4**  
  - `avformat_open_input` → mov demuxer 的 `read_header` 会解析 **moov**、**trak**，建立 `AVStream` 和 sample 索引。  
  - `av_read_frame()` → mov 的 `read_packet` 根据 trak 的索引从 **mdat** 里读出对应区间，填成 `AVPacket`。

- **TS**  
  - open 时 mpegts demuxer 会解析 **PAT/PMT**，建立节目和流的 PID 映射。  
  - `av_read_frame()` → mpegts 的读逻辑按 188 字节读包，按 PID 归类，重组 **PES**，再输出为 `AVPacket`。

---

## 5. 阅读顺序建议

1. 在 **utils.c** 中搜索 `av_read_frame`，读完整函数实现。
2. 找到它调用的 demuxer 接口（如 `read_packet` 或内部封装）。
3. 可选：在 **mov.c** 中搜索 `read_packet` 或 `read_header`，看如何解析 moov/trak 和从 mdat 取数据；在 **mpegts.c** 中看 PAT/PMT 与 PES 如何被解析。

完成以上后，即可回答**作业 1**：av_read_frame() 是如何读取数据包的（从 API 到 demuxer 到 AVPacket 的流程）。
