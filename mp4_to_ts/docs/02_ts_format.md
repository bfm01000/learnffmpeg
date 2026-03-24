# Day 2：TS 封装格式与 MP4 对比

## 1. TS 封装格式概览

MPEG-TS（Transport Stream）是一种**面向传输、流式播放**的封装格式。与 MP4 的“box 树”不同，TS 把数据组织成**固定长度的包**，便于在不可靠信道（如广播、UDP）上传输和同步。

### 1.1 TS 包结构（188 字节）

每个 TS 包固定 **188 字节**：

```
+----------+----------+------------------+
| sync(1B) | header   | payload          |
| 0x47     | 3 byte   | 最多 184 字节    |
+----------+----------+------------------+
```

- **sync**：同步字节，固定 `0x47`，用于在字节流中定位包边界。
- **header**：至少 3 字节，包含 PID、 continuity_counter、是否有 payload、是否带 adaptation field 等。
- **payload**：剩余字节。可能放的是 **PSI**（节目关联表、节目映射表等）或 **PES**（打包的音频/视频基本流）。

因此解析 TS 的典型流程是：在数据流中查找 `0x47`，然后每 188 字节切一个包，再根据 PID 判断是 PAT、PMT 还是音视频 PES。

### 1.2 PAT（Program Association Table）

- 通过固定 **PID 0x0000** 的 TS 包传输。
- 作用：列出当前流里有哪些 **program_number**，以及每个节目对应的 **PMT 的 PID**。
- 解析 TS 时通常先解析 PAT，得到“节目 → PMT PID”的映射。

### 1.3 PMT（Program Map Table）

- 每个节目有独立的 PMT，其 PID 由 PAT 给出。
- 作用：列出该节目下所有**基本流**（视频、音频、字幕等）的 **PID** 和**流类型**（如 H.264、AAC）。
- 有了 PAT + PMT，就能知道“哪个 PID 是视频、哪个是音频”，从而把后续 TS 包按 PID 分类，交给对应解码器。

### 1.4 PES（Packetized Elementary Stream）

- 音视频数据以 **PES 包**的形式放在对应 PID 的 TS 包 payload 里。
- PES 有头部（含 DTS/PTS 等时间戳）和 payload（如一帧或若干 NAL 单元）。
- 解析时：先根据 PAT/PMT 确定音视频 PID，再对相应 PID 的 TS 包重组 PES，再从 PES 里取时间戳和裸流数据。

---

## 2. 与 MP4 的差异对比

| 维度         | MP4                              | TS                                    |
|--------------|----------------------------------|----------------------------------------|
| 结构         | 层级 box（moov/trak/mdat）       | 固定 188 字节的包，线性排列            |
| 元数据       | 集中在 moov（含 trak）           | 分散在 PAT/PMT 及 PES 头中             |
| 媒体数据     | 集中在 mdat，由 trak 索引        | 分布在各个 PID 的 TS 包 payload 中     |
| 随机访问     | 依赖 moov+trak 的 sample table   | 需通过 PAT/PMT 和可选 SI 表定位关键帧  |
| 错误/丢包    | 无内置包概念，依赖上层           | 有 PID、continuity_counter，便于检测丢包 |
| 典型用途     | 点播、存储、下载后播放           | 直播、广播、IPTV、UDP 传输             |
| 文件/流      | 常以“文件”形式存在               | 常以“无限长流”形式存在                 |

### 2.1 结构差异

- **MP4**：先有“目录”（moov + trak），再有“数据”（mdat）；播放器先解析 moov 得到索引，再按索引读 mdat。
- **TS**：没有单一的“目录”块，而是通过 PAT/PMT 持续描述节目和流；数据按 PID 持续到来，边解析边播放。

### 2.2 随机访问

- **MP4**：通过 trak 内的 stco/co64、stts、关键帧表等，可直接跳到任意时间点或关键帧，适合进度条、拖动。
- **TS**：若没有额外索引（如 HLS 的 m3u8 + 分片），随机访问需要扫描或依赖 EPG/关键帧表，通常不如 MP4 直接。

### 2.3 适用场景

- **MP4**：本地/点播视频、短视频、下载、编辑、多轨（多音轨/多字幕）封装。
- **TS**：直播、广播、IPTV、HLS/DASH 的切片格式（每个切片多为 TS）、对丢包和同步要求高的传输场景。

---

## 3. 解析 TS 的简要思路

1. 在字节流中找 `0x47`，按 188 字节切包。
2. 解析 PAT（PID=0x0000），得到 program_number → PMT PID。
3. 解析各 PMT，得到每个节目的 stream_type 与 PID（音/视/字幕等）。
4. 对目标 PID 的包：若为 PES，重组 PES 并解析 PES 头与 payload；若为 SI/PSI 表，按表结构解析。
5. 将 PES payload 按 stream_type 交给对应解码器。

---

## 4. 与 Day 1 的衔接

- Day 1 的 **moov / trak / mdat** 是“一个文件里的一块元数据 + 一块数据”的模型。
- TS 是“持续到来的小包 + 分散的 PAT/PMT 元数据”的模型。  
理解两者差异后，再做**作业 3**（对比与适用场景）会更容易。  
下一步：Day 3 学习 FFmpeg 中 `av_read_frame()` 的解析流程。
