# 从字节视角理解 MP4、AVCC、Annex-B 与 NALU

这是一份给初学者的“从浅到深”版本。你可以按顺序阅读，最后会回到你自己的 `main.cpp`，理解每个关键字节是怎么被解析出来的。

---

## 0. 先回答你最关心的问题

为什么直接解析 `mp4` 不行，而执行这条命令后就可以？

```bash
ffmpeg -i input.mp4 -c:v copy -bsf:v h264_mp4toannexb output.h264
```

一句话答案：

- 你的代码是“按起始码 `00 00 01` 找 NALU”，这是 **Annex-B** 解析法。
- MP4 里的 H.264 往往是 **AVCC（长度前缀）**，不是起始码分隔。
- `h264_mp4toannexb` 把 AVCC 改成 Annex-B，你的代码与数据格式终于匹配，所以结果正常。

---

## 1. 先建立三个核心概念

你提到的 `balu`，基本可以确定你想说的是 **NALU**。

### 1.1 NALU 是什么

NALU（Network Abstraction Layer Unit）是 H.264 里的基本数据单元。  
一个 NALU 可以简单理解成：

```text
[NAL header 1字节][payload 若干字节]
```

头字节里最常用的是低 5 bit：`nal_unit_type`。

- `7` -> SPS
- `8` -> PPS
- `6` -> SEI
- `5` -> IDR slice
- `1` -> non-IDR slice

### 1.2 Annex-B 是什么

Annex-B 是“怎么切 NALU 边界”的一种格式：在每个 NALU 前放起始码。

```text
00 00 01 或 00 00 00 01
```

所以 Annex-B 的比特流长这样：

```text
[start code][NALU][start code][NALU]...
```

### 1.3 AVCC 是什么

AVCC 是另一种切边界方式（MP4 常用）：在每个 NALU 前放长度字段，而不是起始码。

```text
[NALU_length][NALU_payload][NALU_length][NALU_payload]...
```

并且 `avcC`（AVCDecoderConfigurationRecord）里会告诉你长度字段占几字节（常见 4 字节）以及 SPS/PPS。

---

## 2. 你现在的代码在做什么（逐行映射）

你的 `main.cpp` 核心流程可以概括为：

1. 读取文件块到 `buffer`
2. 在 `buffer` 中扫描 `00 00 01` / `00 00 00 01`
3. 找到起始码后，取后一个字节作为 NAL header
4. `header & 0x1F` 得到 `nal_unit_type`
5. 打印类型（SPS/PPS/IDR 等）

你的函数：

```cpp
int get_nalu_type(unsigned char byte){
    return 0x1F & byte;
}
```

就是在做第 4 步。

这套逻辑只在输入是 Annex-B 时可靠。如果输入是 AVCC（比如直接读 mp4），第 2 步就会错。

---

## 3. 从“每个字节”看，为什么会错

## 3.1 Annex-B 的字节序列（你的代码期望）

假设某个 IDR NALU 头字节是 `0x65`：

```text
00 00 00 01 65 ...
```

你的代码扫描到 `00 00 00 01` 后，读到 `65`：

- `0x65 & 0x1F = 0x05`
- type = 5（IDR）

解析正确。

## 3.2 AVCC 的字节序列（MP4 常见）

同一个 NALU 在 AVCC 里通常类似：

```text
00 00 02 A1 65 ...
```

这里前 4 字节 `00 00 02 A1` 是“长度=673”，不是起始码。  
你的扫描器会把整个 MP4 文件当裸流扫，很容易在错误位置“撞到”类似 `00 00 01` 的序列，于是：

- 切边界错
- 读到的“header”其实不是 NAL header
- 得到大量 `type 0 / Other`

这就是你之前输出异常的根本原因。

---

## 4. MP4 里到底装了什么（为什么不能直接扫整个文件）

MP4 是容器，结构大致是 box 树：

```text
[ftyp][moov][mdat]...
```

- `moov`：元数据、索引、样本描述（包括 `avcC`）
- `mdat`：媒体样本数据

关键点：你直接 `ifstream` 读的是“整个容器字节流”，不仅是视频样本。  
在工程里，正确流程是：

1. 先 demux（按 MP4 结构取出视频样本）
2. 再按 AVCC 规则切每个 NALU（读长度字段）
3. 再解析 NAL header / RBSP

这也是 FFmpeg 的 `libavformat + libavcodec` 平时做的事。

---

## 5. `h264_mp4toannexb` 具体做了什么

命令再看一次：

```bash
ffmpeg -i input.mp4 -c:v copy -bsf:v h264_mp4toannexb output.h264
```

逐项解释：

- `-c:v copy`：不重编码（画质不变，速度快）
- `-bsf:v h264_mp4toannexb`：只改“比特流打包方式”

这个 bsf 的关键动作：

1. 将 `length-prefixed NALU` 转为 `start-code NALU`
2. 在需要处补/插 SPS、PPS（基于 `avcC` 信息）
3. 输出裸 `.h264` 数据流（Annex-B）

你可以把它理解为“翻译器”：内容不变，包装换了。

---

## 6. 转换前后结构对照（最实用）

## 6.1 转换前（MP4 + AVCC）

```text
Container: MP4
Video sample: [len][nalu][len][nalu]...
Config: avcC 里存 SPS/PPS 和长度字段大小
```

## 6.2 转换后（裸流 + Annex-B）

```text
Elementary stream: H.264 Annex-B
Bitstream: [00 00 00 01][nalu][00 00 01][nalu]...
```

你的解析器就是给 6.2 设计的，所以转换后立刻正常。

---

## 7. 回到你的输出，为什么现在“看起来对了”

你看到类似：

```text
SEI -> SPS -> PPS -> IDR -> 1 -> 1 -> 1 ...
```

这在实际码流里很常见，说明边界基本切对了。

补一个专业准确性：

- `type 1` 更严谨应称 `non-IDR slice`
- 它不一定等于“P 帧”，也可能是 B（需要继续解析 slice header 才能判断）

---

## 8. 初学者实操检查清单（建议收藏）

当解析结果很乱时，按这个顺序排查：

1. 输入是不是容器文件（`mp4/mkv`）而非裸流（`.h264`）？
2. 当前解析代码是按 Annex-B 还是 AVCC 写的？
3. 若输入是 MP4，是否先 demux/转换成 Annex-B？
4. 边界判断是否越界安全（扫描窗口、跨 buffer 残留）？
5. `type 1` 是否误当作固定 P 帧？

---

## 9. 常用命令

提取为 Annex-B（不重编码）：

```bash
ffmpeg -i input.mp4 -c:v copy -bsf:v h264_mp4toannexb output.h264
```

查看流信息：

```bash
ffprobe -hide_banner -show_streams input.mp4
```

---

## 10. 你接下来可以怎么进阶

如果你希望“直接解析 MP4 而不是先转 `.h264`”，下一步学习路径是：

1. 读 `avcC`（拿到长度字段字节数、参数集）
2. 从 MP4 demux 后逐个 sample 读取 `[length][nalu]`
3. 对每个 nalu 的 header 做 `& 0x1F`，再深入到 RBSP / slice header

走完这条路线，你就从“能跑 demo”进入“能写播放器/分析器核心模块”的阶段了。
