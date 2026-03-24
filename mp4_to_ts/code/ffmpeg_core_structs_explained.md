# FFmpeg 三个核心结构详解（结合 `main.cpp`）

本文结合同目录下的 `main.cpp`（MP4 remux 到 TS 示例）解释三个最关键的数据结构：

- `AVStream`
- `AVCodecParameters`
- `AVPacket`

---

## 1. 先看整体：这三个对象在流水线里的位置

在你的代码里，处理链路可以抽象成：

1. 打开输入容器，拿到很多输入流 `ifmt_ctx->streams[i]`（每个就是一个 `AVStream`）
2. 从每个输入流里读到编码参数 `in_stream->codecpar`（这是 `AVCodecParameters`）
3. 在输出容器创建对应 `AVStream`，并复制 codec 参数到 `out_stream->codecpar`
4. 循环 `av_read_frame` 读出一个个 `AVPacket`
5. 对 `AVPacket` 做流索引映射、时间戳重标定后写出

一句话关系：

- `AVStream`：**“这条流”的容器级描述（视频流/音频流/字幕流）**
- `AVCodecParameters`：**“这条流里编码数据长什么样”的参数快照**
- `AVPacket`：**“这条流里的一小块压缩数据”**

---

## 2. `AVStream` 是什么，有什么用

## 定义角色

`AVStream` 代表“容器中的一条媒体流”。  
比如一个 MP4 里**常见**有：

- 流 0：视频（H.264）
- 流 1：音频（AAC）

每条流都有自己的编号、时基、时长信息、参数等。

## 关于“流数量与顺序”的关键补充

- MP4 **不一定只有两条流**。常见是 1 视频 + 1 音频，但也可能有多音轨、字幕轨、附加数据轨等。
- `stream_index`（0、1、2...）是容器内部记录顺序，**不是语义固定编号**。
- 因此不存在“永远 0=视频、1=音频”的保证；有些文件可能 0 是音频、1 才是视频。
- 实战中应按 `codec_type` 判断流类型，而不是写死索引。

## 在本代码中的体现

- 遍历输入流：
  - `AVStream* in_stream = ifmt_ctx->streams[i];`
- 创建输出流：
  - `AVStream* out_stream = avformat_new_stream(ofmt_ctx, nullptr);`
- 读包后按包所属流取信息：
  - `AVStream* in_stream = ifmt_ctx->streams[pkt.stream_index];`
  - `AVStream* out_stream = ofmt_ctx->streams[pkt.stream_index];`

## 它主要解决什么问题

1. **流级别组织**：区分“这个包属于视频还是音频”
2. **时间基准**：每条流有自己的 `time_base`
3. **挂载编码参数**：通过 `stream->codecpar` 告诉 muxer/demuxer 这条流的编码配置

---

## 3. `AVCodecParameters` 是什么，有什么用

## 定义角色

`AVCodecParameters` 是“编码参数描述”，不含真正媒体数据。  
它像“格式说明书”，典型字段包含：

- `codec_type`：音频/视频/字幕
- `codec_id`：如 H.264、AAC
- 视频维度：宽、高、像素格式（部分场景）
- 音频维度：采样率、声道布局（部分场景）
- `extradata`：编解码初始化信息（如 SPS/PPS 等）

## 在本代码中的体现

- 读取输入参数：
  - `AVCodecParameters* in_par = in_stream->codecpar;`
- 过滤流类型：
  - 只保留 `AVMEDIA_TYPE_AUDIO / VIDEO / SUBTITLE`
- 拷贝到输出流：
  - `avcodec_parameters_copy(out_stream->codecpar, in_par);`
- 清理容器私有 tag：
  - `out_stream->codecpar->codec_tag = 0;`

## 为什么 remux 必须处理它

remux（不转码）本质是“搬运压缩码流到新容器”。  
要让输出容器正确写 header，必须先把每条输出流的 codec 参数准备好，`avformat_write_header` 才知道怎么写。

所以你的流程是：

- 先把 `codecpar` 准备好
- 再 `avformat_write_header`
- 再写真实 `AVPacket`

---

## 4. `AVPacket` 是什么，有什么用

## 定义角色

`AVPacket` 是“压缩数据包”。  
它不是解码后像素/PCM，而是编码后的一段数据（例如某个视频 access unit 的码流，或一段音频压缩帧）。

常见关键字段：

- `data/size`：压缩数据与长度
- `stream_index`：属于哪条流
- `pts/dts/duration`：时间戳相关
- `pos`：在原输入中的字节位置（可无效）

## 在本代码中的体现

主循环做的是“逐包搬运”：

1. `av_read_frame(ifmt_ctx, &pkt)` 读一个包
2. 检查流映射，不需要就丢弃并 `av_packet_unref(&pkt)`
3. 把 `pkt.stream_index` 改成输出流索引
4. `av_packet_rescale_ts(&pkt, in_stream->time_base, out_stream->time_base)` 重标定时间戳
5. `pkt.pos = -1` 清掉无意义的输入偏移
6. `av_interleaved_write_frame(ofmt_ctx, &pkt)` 写出
7. `av_packet_unref(&pkt)` 释放包引用

## 为什么必须 `av_packet_unref`

`AVPacket` 里很多数据是引用计数管理的缓冲区。  
每次处理完都要 `av_packet_unref`，否则长时间循环会内存增长甚至泄漏。

## 关于“1 帧和 1 个 AVPacket”的关系

- 在常见 MP4 的视频流里，通常可近似理解为 **1 个视频 packet 对应 1 帧（更准确是 1 个 access unit）**。
- 但这不是跨所有格式/场景的硬性保证；在某些封装或裸流场景中，可能出现“跨包组帧”或“1 包含多帧”。
- 对你这个 remux 示例，工程上通常不需要手写拼帧逻辑，按 `av_read_frame` 的包粒度处理即可。

---

## 4.1 为什么要循环读包：和 TS 188 字节是什么关系

很多初学者会把这两件事混在一起：

- `AVPacket`：FFmpeg 在 demux/mux 层处理的“媒体压缩数据包”
- TS 188 字节包：MPEG-TS 传输层固定大小的“传输单元”

二者不是同一层，通常不是 1:1 对应。

在你的代码里，循环处理的是 `AVPacket`：

1. `av_read_frame` 读一个输入 `AVPacket`
2. 改索引、重映射时间戳
3. `av_interleaved_write_frame` 交给 TS muxer

随后 TS muxer 才会把这个 `AVPacket` 封装/切分成若干个 188 字节 TS 包写出。  
所以必须循环直到 EOF，才能把整段媒体完整搬完。

---

## 5. 三者关系（非常重要）

可以把它们理解成三层：

1. **流层（AVStream）**
   - “这是第几条流、时基是什么、参数挂在哪”
2. **参数层（AVCodecParameters）**
   - “这条流编码属性是什么（H.264/AAC、宽高、采样率、extradata 等）”
3. **数据层（AVPacket）**
   - “这条流当前这一包具体数据是什么、时间戳是多少”

对应到生活类比：

- `AVStream`：一条物流通道
- `AVCodecParameters`：这条通道的运输规格
- `AVPacket`：通道里每一件实际货物

---

## 6. 结合你的代码再看一遍最小闭环

1. 用 `AVStream` 找到每条输入流
2. 从输入 `AVStream` 取 `AVCodecParameters`
3. 建立输出 `AVStream` 并复制参数
4. 写 header（muxer 根据输出流参数初始化）
5. 循环读 `AVPacket`，按目标流与时基修正后写出
6. 每个包都 unref，最后写 trailer

这就是典型的 **stream copy / remux** 流程。

---

## 7. 常见误区（你现在这个阶段最值得注意）

1. **误区：`AVPacket` 就是一帧解码图像**
   - 不是。它是“压缩数据包”，解码后才会变成 `AVFrame`。

2. **误区：只复制 `AVPacket`，不管 `AVCodecParameters` 也能写**
   - 不行。输出 header 依赖每条输出流的 codec 参数。

3. **误区：`pts/dts` 可以直接原样写**
   - 不一定。输入输出流 `time_base` 不同就要 `av_packet_rescale_ts`。

4. **误区：`av_packet_unref` 可省略**
   - 循环里省略通常会导致内存问题。

5. **误区：`stream_index` 不改也行**
   - 不行。输出 muxer 需要的是“输出流索引”，不是输入流索引。

6. **误区：MP4 永远只有两个流，且 0=视频、1=音频**
   - 不对。流数量和顺序都不固定，应该通过 `codecpar->codec_type` 判断流类型。

7. **误区：拿到 `AVCodecParameters` 就要手动解析 SPS/PPS**
   - 通常不需要。对 MP4，demuxer 会把容器中的相关参数整理到 `codecpar/extradata`，常规 remux 直接使用即可；只有更底层分析需求才手动逐字节解析。

---

## 8. FAQ（针对本项目最常问的问题）

1. **一个 MP4 一般只有两条流吗？**
   - 常见是 1 视频 + 1 音频，但并不保证。可能有多音轨、字幕、数据轨。

2. **`stream_index` 的顺序固定吗？一定 0=视频、1=音频？**
   - 不固定。`stream_index` 是容器记录顺序，不是语义常量。应看 `codec_type`。

3. **拿 `AVCodecParameters` 必须自己解析 SPS/PPS 吗？**
   - 通常不必。`avformat_open_input + avformat_find_stream_info` 后，常见 MP4 已可从 `codecpar/extradata` 获取关键参数。

4. **一个流里大概有多少个 `AVPacket`？**
   - 无固定值，和时长、码率、帧率、音频打包粒度有关。代码里应“读到 EOF 为止”，不要预设总包数。

5. **需要重点考虑“一帧拆成多个 AVPacket”吗？**
   - 在当前常见 MP4->TS remux 场景通常不用；按包处理即可。仅在特殊码流/封装场景才需要做额外组帧逻辑。

---

## 9. 一句话记忆版

- `AVStream`：**流的身份与时间体系**
- `AVCodecParameters`：**流的编码说明书**
- `AVPacket`：**流里逐个搬运的压缩数据包**

当你做 MP4 -> TS remux 时，实际上是在：

“先为每条输出流准备好说明书，再把每个包按新时间体系和流编号搬过去。”

