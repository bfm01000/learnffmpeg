# 05 - H.264、MP4、NALU 与 AVCC / Annex-B

> 对应导读第 3.1 节"压缩问题"、第 3.2 节"组织问题"、第 6.5 节"AVCC vs Annex-B"。
> 这一篇覆盖：编码层 vs 容器层的差别、H.264 的 NALU 结构、MP4 怎么装 H.264、AVCC 和 Annex-B 两种打包方式、I/P/B 帧 + GOP、PTS / DTS、SPS / PPS 补齐、AAC 的 ADTS、`-c copy` 的本质。
> 这是面试**绝对高频考点**，几乎每个音视频岗位都会问到。

---

## 一、先建立一个最基本的认知：编码 ≠ 封装

这是入门第一道分水岭。**H.264 和 MP4 是两个不同的层**：

| 层 | 关心什么 | 典型代表 |
|---|---|---|
| 编码层（Codec） | 怎么压缩图像 / 声音 | H.264、H.265、AV1、AAC、Opus、MP3 |
| 容器层（Container） | 怎么组织、对齐多路流、提供时间戳和索引 | MP4、FLV、TS、MKV、WebM、AVI |

- **H.264 关心压缩**，产物是 NALU 字节流。
- **MP4 关心组织**，关心轨道、时间戳、索引、元数据。

可以把 H.264 理解成"内容本身"，MP4 是"带目录和时间轴的包装"。

> 关键推论：**同一段视频可以"编码不变、容器变化"**。你用 `ffmpeg -i a.mp4 -c copy a.flv` 把 MP4 转成 FLV，根本没动编码字节，只换了壳子。

---

## 二、MP4 转 H.264 是不是转码

**先给结论**：通常不是。

**前提**：MP4 视频轨本身就是 H.264。

**那做的是什么**：解封装（demux）+ 码流重打包。

```
MP4
 |  demux: 按 moov 索引从 mdat 取出视频样本
 v
H.264 字节 (AVCC 格式)
 |  AVCC -> Annex-B
 v
H.264 字节 (Annex-B 格式) + 插入 SPS/PPS
 |
 v
output.h264 (裸流文件)
```

整个过程**没有解码也没有重新编码**——只是从一种打包方式翻译到另一种打包方式。

只有当**编码不一致**（如 HEVC → H.264）或**业务需要改编码参数**（分辨率、码率、帧率、GOP）时，才需要真正的转码（decode → encode）。

---

## 三、NALU 是什么

NALU（Network Abstraction Layer Unit）是 H.264 的基本数据单元。它的结构最简：

```
[NAL header 1字节][payload 若干字节]
```

NAL header 的低 5 bit 是 `nal_unit_type`，决定这个 NALU 装的是什么：

| type | 名称 | 含义 |
|---|---|---|
| 1 | non-IDR slice | 普通帧（P 帧或 B 帧的 slice） |
| 5 | IDR slice | 即时解码刷新帧（特殊的 I 帧，独立可解码） |
| 6 | SEI | 补充增强信息（HDR 元数据、时间码、私有 metadata） |
| 7 | SPS | Sequence Parameter Set（序列参数集，描述整段视频的宽高、profile、level） |
| 8 | PPS | Picture Parameter Set（图像参数集，描述编码细节） |

解码 H.264 的程序逻辑大致：

```cpp
int getNaluType(unsigned char byte) {
    return byte & 0x1F;   // 低 5 bit
}
```

---

## 四、AVCC vs Annex-B：两种切边界方式

无数 NALU 连在一起就是 H.264 流。问题是**怎么区分哪到哪是一个完整的 NALU**？业界有两种标准。

### 4.1 Annex-B（直播流格式）

每个 NALU 前加一个**起始码**：

```
[startcode][NALU][startcode][NALU][startcode][NALU]...
```

起始码是 `00 00 01` 或 `00 00 00 01`。解码器扫到这个标志就知道新 NALU 开始了。

SPS / PPS 也作为普通 NALU **混在数据流里**，可以周期性出现。

**典型使用方**：RTMP、TS、RTP / WebRTC、裸 `.h264` 文件。

### 4.2 AVCC（MP4 存储格式）

每个 NALU 前加 **4 字节（也可能 1 / 2 字节）长度信息**：

```
[NALU_length (4字节)][NALU_payload][NALU_length][NALU_payload]...
```

SPS / PPS **不在数据流里**——单独抽出来放在 MP4 的 `avcC` 配置（即 `extradata`）里。

**典型使用方**：MP4、FLV、MKV。

### 4.3 两种格式对照

| 维度 | AVCC | Annex-B |
|---|---|---|
| NALU 切割 | 长度前缀 | 起始码 `00 00 01` |
| SPS / PPS 位置 | `avcC` extradata 集中存放 | 数据流里周期性出现 |
| 长度字段占几字节 | 看 `avcC` 配置（一般 4） | 无 |
| 寻找下一个 NALU | 直接按长度跳过 | 扫描起始码 |
| 典型容器 / 协议 | MP4 / FLV / MKV | RTMP / TS / RTP / 裸流 |

---

## 五、为什么直接解析 MP4 字节会出错（经典 bug 现场）

新人常写出这种代码：

```cpp
// 读 .mp4 文件,扫描 00 00 01 找 NALU
std::ifstream file("input.mp4", std::ios::binary);
// ... 按起始码扫描 ...
```

**这套逻辑只在输入是 Annex-B 时可靠**。MP4 里是 AVCC，扫描会撞到错误的 `00 00 01`（其实那可能是 NALU 长度字段的一部分），切边界全乱，输出大量 type 0 / Other。

### 5.1 用 ffmpeg 命令"翻译"一下

```bash
ffmpeg -i input.mp4 -c:v copy -bsf:v h264_mp4toannexb output.h264
```

- `-c:v copy`：不重新编码
- `-bsf:v h264_mp4toannexb`：bitstream filter，**只改打包方式**

这个 bsf 做了三件事：

1. 把 length-prefixed NALU 转成 start-code NALU
2. 在合适位置注入 SPS / PPS（从 `avcC` 取出来）
3. 输出裸 `.h264` 数据流（Annex-B）

**内容字节没变，只换了壳子**。

### 5.2 MP4 内部结构（最简骨架）

MP4 是 box 树：

```
[ftyp]   <- 文件类型
[moov]   <- 元数据 / 轨道描述 / 索引
   +-- trak (视频)
   |    +-- stsd (samples 描述,含 avcC -> SPS/PPS)
   |    +-- stco (sample 偏移)
   |    +-- stsz (sample 大小)
   |    +-- stts (时间戳信息)
   |    +-- stss (关键帧索引)
   +-- trak (音频)
[mdat]   <- 实际媒体数据 (一堆 AVCC NALU)
```

**关键点：`mdat` 不能脱离 `moov` 独立解释**——sample 的偏移、大小、时序、关键帧索引全在 `moov` 的表里。直接读 `mdat` 你不知道每个 sample 多长、什么时候播。

---

## 六、SPS / PPS 补齐：直播 / 实时场景的关键

### 6.1 为什么必须补齐

解码器**在解码任何帧之前必须先拿到 SPS / PPS**，否则可能直接解码失败或花屏。

- MP4 文件里 SPS / PPS 集中放在 `avcC` extradata 里，文件能从头解析就没问题。
- 但**直播流中途切入**（新观众加入）、**RTSP 重连**、**seek 到中间**这些场景，观众进来时如果错过 SPS / PPS，会**黑屏 / 花屏直到下一次 SPS / PPS 出现**。

### 6.2 提取来源

MP4 里 SPS / PPS 在 `avcC` 配置中：

```c
// avcC 结构(简化)
{
    configurationVersion: 1,
    AVCProfileIndication: 0x42,    // Baseline = 0x42, Main = 0x4D, High = 0x64
    profile_compatibility: 0x00,
    AVCLevelIndication: 0x1E,      // Level 3.0
    lengthSizeMinusOne: 3,          // 长度字段是 4 字节(3+1)
    numOfSequenceParameterSets,     // 然后是 SPS 列表
    sps_list[...],
    numOfPictureParameterSets,
    pps_list[...]
}
```

转 Annex-B 时把这些 SPS / PPS 提取出来，包装成起始码 NALU 注入到输出码流。

### 6.3 注入策略

| 策略 | 优点 | 缺点 |
|---|---|---|
| 只在文件头注入一次 | 简单 | 随机切入弱，丢包后无法恢复 |
| **每个 IDR 前注入** | **稳，工程上首选** | 体积略增（每个 IDR 多几十字节） |
| 周期性注入 | 折中 | 实现稍复杂 |

**直播链路几乎一定选每个 IDR 前注入**。带宽开销极小，换来的是中途入会和弱网恢复成功率大幅提升。

---

## 七、I / P / B 帧 + GOP

### 7.1 三种帧

- **I 帧**（Intra-coded）：帧内预测编码。完整画面，**不依赖其他帧**，是解码起点。数据量最大。
- **P 帧**（Predictive）：前向预测帧。表示与前面 I/P 帧的差别。**依赖前面**。
- **B 帧**（Bi-directionally predictive）：双向预测帧。**同时依赖前后**的 P/I 帧。数据量最小，压缩率最高。

#### 帧内预测 ≠ JPEG 压缩

很多人以为 I 帧就是单张 JPEG。**现代 I 帧用了帧内预测（Intra Prediction）**——利用同一帧内已编码的相邻像素（通常是当前宏块的上方和左侧）预测当前块，只对差值（残差）编码。比纯 JPEG 压缩率高得多。

#### 参考 B 帧 vs 普通 B 帧（高级考点）

H.264 / H.265 把 B 帧细分两种：

| 类型 | 是否被其他帧参考 | 弱网时能否丢 |
|---|---|---|
| **普通 B 帧**（Non-reference） | 不被任何帧参考 | **可以随意丢**，顶多画面不流畅 |
| **参考 B 帧**（Reference B / 分层 B） | **被其他 B 帧参考** | **绝对不能丢**，丢了依赖它的子帧会花屏 |

判断方法：解析 NALU header 里的 `nal_ref_idc` 字段。**直播弱网降帧策略必须分辨这两种**——丢错了直接花屏。

### 7.2 IDR 帧 ≠ I 帧

IDR（Instantaneous Decoder Refresh）是一种**特殊的 I 帧**：

- 普通 I 帧：自己能解，但后续帧可能参考它之前的帧
- **IDR 帧：自己能解，并且强制清空之前的所有参考帧缓冲**——之后的帧绝对不会参考 IDR 之前的任何帧

直播 seek、随机切入只能跳到 IDR 帧，不能跳到普通 I 帧。

### 7.3 GOP（Group of Pictures）

一组连续画面，由一个 I 帧开始，到下一个 I 帧之前结束。

GOP 结构常用两个数字描述：

- `M`：两个 P 帧之间的 B 帧数量
- `N`：GOP 长度（两个 I 帧之间的距离）

例：`M=3, N=15` 表示 GOP 长度 15，每 3 帧一个 P，中间塞 2 个 B。

| 场景 | GOP 策略 |
|---|---|
| 直播（首屏秒开、低延迟） | **短 GOP**（1-2 秒）。让新观众更快遇到 I 帧 |
| 点播（追求压缩率） | **长 GOP**（5-10 秒甚至更长）。I 帧少，整体码率低 |

---

## 八、PTS 与 DTS

### 8.1 为什么有两个时间戳

```
显示顺序:    I  B  B  P  B  B  P  ...
解码顺序:    I  P  B  B  P  B  B  ...
```

B 帧需要参考它后面的 P 帧才能解码。所以**解码器必须先解 P 帧再解 B 帧，但显示时 B 帧要在 P 帧前面**。

- **PTS**（Presentation Time Stamp）：什么时候显示这帧
- **DTS**（Decoding Time Stamp）：什么时候解码这帧

无 B 帧时 `PTS == DTS`；**有 B 帧时 `PTS ≠ DTS`，解码器要按 DTS 顺序解码，渲染器按 PTS 顺序显示**。

### 8.2 单位是刻度不是秒

详见 [00-FFmpeg全景导读.md](./00-FFmpeg全景导读.md) 第 6.2 节。PTS / DTS 是整数，单位由所在流的 `time_base` 决定：

```
真实时间 = pts × time_base
```

跨模块时用 `av_rescale_q` 换算。

---

## 九、AAC 的 ADTS（音频对照点）

视频有 NALU 这个边界单元，音频有什么对应物？答案是 **ADTS（Audio Data Transport Stream）**。

### 9.1 裸 AAC 不能直接解码

裸 AAC 只是压缩字节。直接丢给解码器，它不知道：

- 采样率多少
- 几个声道
- 是 AAC-LC 还是 HE-AAC

### 9.2 ADTS 给每帧穿一件 7 字节马甲

ADTS Header 7 字节，包含：

- 同步字 `0xFFF`（定位帧开头）
- 采样率索引
- 声道数
- 这一帧的长度
- profile / version 信息

### 9.3 MP4 里是裸 AAC，直播里必须 ADTS

| 场景 | 格式 | 为什么 |
|---|---|---|
| MP4 容器里的 AAC | **裸 AAC** | 参数集中在文件头 `esds` box，每帧不需要重复 |
| TS / 直播流里的 AAC | **ADTS** | 观众随时切入，每帧必须自带说明书 |

这和视频的 AVCC vs Annex-B 是**完全对应的设计**——存储优化 vs 传输鲁棒性的取舍。

---

## 十、常用 ffmpeg 命令

### 10.1 无损重封装

```bash
# MP4 -> 裸 H.264
ffmpeg -i input.mp4 -an -c:v copy -bsf:v h264_mp4toannexb output.h264

# 裸 H.264 -> MP4 (必须指定帧率)
ffmpeg -framerate 25 -i input.h264 -c:v copy output.mp4

# MP4 -> FLV (容器换壳)
ffmpeg -i input.mp4 -c copy output.flv

# MP4 -> TS (直播切片用)
ffmpeg -i input.mp4 -c copy -bsf:v h264_mp4toannexb output.ts
```

### 10.2 为什么裸流转 MP4 要指定 framerate

裸 `.h264` **只有编码字节，没有时间信息**。MP4 muxer 要写 `stts / ctts` 表必须知道帧率。不指定 framerate，输出 MP4 可能时长不对、播放快慢异常。

### 10.3 查看流信息

```bash
ffprobe -hide_banner -show_streams input.mp4
ffprobe -hide_banner -show_packets input.mp4 | head -30
ffprobe -hide_banner -show_frames -select_streams v:0 input.mp4 | head
```

---

## 十一、工程验证清单

转换或重打包之后，建议这样验证：

1. `ffprobe` 看 codec / 时长 / 帧率 / 关键帧分布
2. 播放器随机拖动进度条，重点测关键帧切入是否稳定
3. 直播链路专门测中途入会和弱网恢复
4. 确认 SPS / PPS 注入策略有效（用 ffprobe 看 packet 流，关键帧前应有 SPS+PPS）

---

## 十二、3 分钟口述模板（面试用）

"MP4 和 H.264 是不同层：H.264 是编码标准，MP4 是容器。

所以 MP4 转 H.264 在视频轨本身已是 H.264 时**通常不是转码**，而是解封装加码流重打包。流程是先按 `moov` 的采样表从 `mdat` 提取视频样本，再把 AVCC 的长度前缀 NALU 转成 Annex B 起始码格式，并从 `avcC` 取 SPS/PPS 按需注入——工程上常在每个 IDR 前注入提升随机切入稳定性，最后输出裸 `.h264`。

反过来 H.264 到 MP4 是重封装：解析 Annex B NALU、提取参数集写入 `avcC`、构建 PTS/DTS 和 `stts/ctts/stsz/stco/stss` 表后写到 `moov`，样本写到 `mdat`。

关键风险是时序构建和参数集可达性，尤其 B 帧重排的 PTS/DTS 关系，以及裸流封装时帧率设置——否则会出现解码失败或播放速度异常。

只有当编码不一致或需要改编码参数时，才需要真正的转码。"

---

## 十三、自检（高频面试题改编）

1. MP4 转 H.264 是不是转码？前提条件是什么？
2. AVCC 和 Annex-B 一句话怎么概括差别？
3. 把 MP4 里的字节直接喂给 Android MediaCodec 会怎样？为什么？怎么解决？
4. SPS / PPS 通常存在哪里？为什么直播场景要每个 IDR 前都注入？
5. I 帧和 IDR 帧的区别是什么？为什么 seek 只能跳到 IDR？
6. 普通 B 帧和参考 B 帧弱网时哪个能丢？怎么判断？
7. 为什么会有 PTS 和 DTS 两个时间戳？什么时候相等？
8. GOP 长度对首屏秒开和压缩率分别有什么影响？
9. AAC 在 MP4 里和在 TS 直播流里的格式分别是什么？为什么？
10. `ffmpeg -c copy` 到底做了什么？什么时候不能用？
