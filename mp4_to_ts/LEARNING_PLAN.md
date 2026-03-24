# MP4 / TS 封装格式与 FFmpeg 解析学习计划

本学习计划帮助你掌握 **MP4 / TS 封装格式** 以及 **通过 FFmpeg 源码解析媒体文件**，包含每日目标、讲解与作业。

---

## 总览

| 天数 | 主题 | 核心目标 |
|------|------|----------|
| Day 1 | MP4 封装格式基础 | 理解 MP4 文件结构，定位 moov / trak / mdat |
| Day 2 | TS 封装格式与对比 | 理解 TS 包结构，与 MP4 对比差异与场景 |
| Day 3 | FFmpeg 解析流程 | 阅读 utils.c，理解 av_read_frame() 调用链 |
| Day 4 | 实践与作业 | 完成解析程序与对比总结 |
| Day 5 | MP4 转 TS 转码实战 | 掌握使用 FFmpeg 将 MP4 转为 TS 的方法与参数 |
| Day 6 | C++ 代码转换实战 | 掌握在 C++ 中调用 FFmpeg API 实现 MP4 转 TS |

---

## 目录结构

```
mp4_to_ts/
├── LEARNING_PLAN.md          # 本文件：总计划与日程
├── docs/
│   ├── 01_mp4_format.md      # Day 1：MP4 格式讲解
│   ├── 02_ts_format.md       # Day 2：TS 格式讲解与对比
│   ├── 03_ffmpeg_demux.md    # Day 3：FFmpeg 解析流程讲解
│   ├── 04_mp4_to_ts_transcode.md # Day 5：MP4 转 TS 实战（命令行）
│   └── 05_cpp_mp4_to_ts.md   # Day 6：C++ 调用 FFmpeg API 转换
├── assignments/
│   ├── README.md             # 作业说明与提交要求
│   ├── assignment_1.md       # 作业 1：av_read_frame 解析
│   ├── assignment_2.md       # 作业 2：MP4/TS 解析程序
│   ├── assignment_3.md       # 作业 3：格式对比与场景
│   ├── assignment_4.md       # 作业 4：MP4 转 TS 转码实战
│   └── assignment_5.md       # 作业 5：C++ 代码实现 MP4 转 TS
└── solutions/                # 参考答案（可选，自学用）
```

---

## 每日学习目标与内容

### Day 1：MP4 封装格式

**学习目标：**
- 理解 MP4 基于 box 的层级结构
- 能说明 `ftyp`、`moov`、`trak`、`mdat` 的作用与关系
- 能在二进制/工具中定位这些 box 的位置与大小

**学习内容：**
- 阅读 `docs/01_mp4_format.md`
- 使用十六进制编辑器或 `xxd` 查看一个 MP4 文件前 1KB，找到 `ftyp`、第一个 `moov`/`mdat` 的偏移和 size

**验证方式：**
- 能口头/书面解释：moov 存元数据、trak 存轨道、mdat 存实际媒体数据
- 完成 `assignments/assignment_2.md` 中「仅 MP4 部分」的解析与打印

---

### Day 2：TS 封装格式与对比

**学习目标：**
- 理解 TS 固定 188 字节包、PAT/PMT/PES 的概念
- 能说明 TS 与 MP4 在结构、随机访问、流式传输上的差异
- 知道 MP4 与 TS 的典型应用场景

**学习内容：**
- 阅读 `docs/02_ts_format.md`
- 用 FFprobe 或简单脚本查看一个 .ts 文件的包长度、PID 分布

**验证方式：**
- 完成 `assignments/assignment_3.md`（对比与场景）
- 在解析程序（作业 2）中增加对 TS 的基本解析（如包数、部分 PID 统计）

---

### Day 3：FFmpeg 解析流程

**学习目标：**
- 理解 `av_read_frame()` 在 demux 流程中的位置
- 能描述其调用链：`av_read_frame` → demuxer → 返回 `AVPacket`
- 知道 `libavformat/utils.c` 中与读包相关的逻辑

**学习内容：**
- 阅读 `docs/03_ffmpeg_demux.md`
- 在 FFmpeg 源码中打开 `libavformat/utils.c`，查找 `av_read_frame` 并沿调用链阅读

**验证方式：**
- 完成 `assignments/assignment_1.md`（av_read_frame 解析说明）

---

### Day 4：综合实践与作业收尾

**学习目标：**
- 整合前三天内容，完成一个小型解析器
- 能对 MP4 打印 moov/trak/mdat 信息，对 TS 打印包结构/部分 PID 信息
- 完成三份作业并自检

**学习内容：**
- 实现 `assignments/assignment_2.md` 的完整要求
- 整理并提交 `assignment_1` 与 `assignment_3` 的答案

**验证方式：**
- 运行解析程序对至少一个 MP4 和一个 TS 文件输出正确信息
- 对比文档与适用场景描述清晰、有依据

---

### Day 5：MP4 转 TS 转码实战

**学习目标：**
- 掌握使用 FFmpeg 将 MP4 转换为 TS 的常见命令
- 理解“直接封装转换（stream copy）”与“重新编码（transcode）”的区别
- 能根据场景选择关键参数（码率、GOP、音频编码、TS 相关标志）

**学习内容：**
- 阅读 `docs/04_mp4_to_ts_transcode.md`
- 实操 3 组命令：  
  1) MP4 → TS（尽量不重编码）；  
  2) MP4 → TS（视频重编码 H.264）；  
  3) 输出多段 TS（为 HLS/分发做准备）

**验证方式：**
- 完成 `assignments/assignment_4.md`
- 使用 `ffprobe` 对输入/输出做对比，能解释主要参数和结果差异

---

### Day 6：C++ 代码实现 MP4 转 TS

**学习目标：**
- 掌握 `libavformat/libavcodec/libavutil` 的最小转换流程
- 能在 C++ 程序中实现 MP4 → TS 的封装转换（remux）
- 理解时间戳重映射（`av_packet_rescale_ts`）与写包流程

**学习内容：**
- 阅读 `docs/05_cpp_mp4_to_ts.md`
- 实现一个最小 C++ 转换程序：打开输入 MP4，创建 TS 输出，循环读包并写入
- 可选进阶：为 H.264 视频流增加 `h264_mp4toannexb` bitstream filter

**验证方式：**
- 完成 `assignments/assignment_5.md`
- 运行程序生成 `output.ts`，并用 `ffprobe` 验证输出流信息

---

## 作业与验证标准

| 作业 | 文件 | 验证目标 |
|------|------|----------|
| 作业 1 | `assignments/assignment_1.md` | 能解释 av_read_frame() 如何读取数据包 |
| 作业 2 | `assignments/assignment_2.md` | 程序能解析 MP4/TS 并打印 moov/trak/mdat 等基本信息 |
| 作业 3 | `assignments/assignment_3.md` | 能对比 MP4 与 TS 的不同点及适用场景 |
| 作业 4 | `assignments/assignment_4.md` | 能完成 MP4 到 TS 的封装转换与重编码实践，并解释参数 |
| 作业 5 | `assignments/assignment_5.md` | 能用 C++ 调用 FFmpeg API 完成 MP4 到 TS 的转换并解释核心代码 |

详细题目与要求见 `assignments/README.md` 及各 `assignment_*.md` 文件。

---

## 推荐 FFmpeg 源码位置

- **媒体解析入口**：`libavformat/utils.c`（如 `av_read_frame`）
- **MP4 demuxer**：`libavformat/mov.c`（可配合理解 moov/trak/mdat）
- **TS demuxer**：`libavformat/mpegts.c`（可配合理解 TS 包解析）

建议先完成 `utils.c` 与 `av_read_frame`，再按需阅读 mov.c / mpegts.c。

---

## 使用方式

1. 按 Day 1 → Day 6 顺序学习，每天先读对应 `docs/` 下的讲解再动手。
2. 作业可随学习进度分步完成：Day 1 后做作业 2 的 MP4 部分，Day 2 后做 TS 部分与作业 3，Day 3 后做作业 1。
3. Day 4 用于收尾解析作业，Day 5 完成命令行转码（作业 4），Day 6 完成 C++ API 实战（作业 5）。

祝你学习顺利。如有疑问，可把具体段落或代码贴出再针对性解答。
