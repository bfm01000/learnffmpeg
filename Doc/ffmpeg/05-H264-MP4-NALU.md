# 05 - H.264、MP4、NALU 与 AVCC / Annex-B

## 0. 本篇定位

| 项 | 说明 |
|---|---|
| 面试位置 | 编码码流与容器边界：H.264、MP4、NALU、SPS/PPS、bitstream filter。 |
| 先背什么 | MP4 里的 H.264 为什么不能直接按 00 00 01 扫，AVCC 和 Annex-B 怎么互转。 |
| 深入怎么学 | 结合 PTS/DTS、GOP、IDR、SPS/PPS 注入和推流/切片场景理解。 |
| 关联阅读 | 06、11、12、19 |

---

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

> **再记一条**：不管 H.264 以裸流（`.h264`）还是 MP4 视频轨形式出现，**编码层的基本单元都是 NALU**。裸流与 MP4 的差别不在"单元是什么"，而在 **NALU 怎么串、边界怎么切**——两种主流切法叫 **Annex-B** 和 **AVCC**（第三节速览、第四节展开）。

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

本质是 **AVCC → Annex-B 的重打包**：NALU 的 payload 字节不变，只是去掉长度前缀、换上起始码，并把 `avcC` 里的 SPS/PPS 插回流里。

只有当**编码不一致**（如 HEVC → H.264）或**业务需要改编码参数**（分辨率、码率、帧率、GOP）时，才需要真正的转码（decode → encode）。

---

## 三、NALU 是什么

NALU（Network Abstraction Layer Unit）是 H.264 的基本数据单元——**裸流和 MP4 里都是它**，差别只在下面两种打包方式怎么切边界。单个 NALU 的结构最简：

```
[NAL header 1字节][payload 若干字节]
```

### 两种打包方式（NALU 不变，切法变）

无数 NALU 连起来就是 H.264 字节流。业界用两种标准来切边界——**NALU 本身不变，变的只是切法**：

#### Annex-B（裸流 / 直播常见）

```
00 00 01 [NALU] 00 00 01 [NALU] ...
```

- 用起始码 `00 00 01`（3 字节）或 `00 00 00 01`（4 字节）分隔——**两种都合法**，何时用哪种见 [§4.1](#41-annex-b直播流格式)
- SPS / PPS 通常混在流里，周期性出现

#### AVCC（MP4 常见）

```
[4字节长度][NALU payload][4字节长度][NALU payload] ...
```

- 用长度前缀分隔，不靠起始码
- SPS / PPS 往往抽到 `avcC` / `extradata`，不在每帧数据里重复

> MP4 转 `.h264` 通常不是转码，而是 demux + **AVCC → Annex-B** 重打包，NALU 内容本身不变（见第二节）。

NAL header 的低 5 bit 是 `nal_unit_type`，决定这个 NALU 装的是什么：

| type | 名称 | 含义 |
|---|---|---|
| 1 | non-IDR slice | 普通帧（P 帧或 B 帧的 slice） |
| 5 | IDR slice | 即时解码刷新帧（特殊的 I 帧，独立可解码） |
| 6 | SEI | 补充增强信息（HDR 元数据、时间码、私有 metadata；WebRTC 常借它透传自定义数据/时间戳） |
| 7 | SPS | Sequence Parameter Set（序列参数集，描述整段视频的宽高、profile、level） |
| 8 | PPS | Picture Parameter Set（图像参数集，描述编码细节） |
| 9 | AUD | 访问单元分隔符（标记一帧边界，直播流里常见） |
| 12 | Filler | 填充数据（CBR 凑码率用，无实际内容） |

解码 H.264 的程序逻辑大致：

```cpp
int getNaluType(unsigned char byte) {
    return byte & 0x1F;   // 低 5 bit —— 仅 H.264
}
```

> **迁移警告**：`byte & 0x1F` **只对 H.264 成立**。H.265 的 NAL header 是 **2 字节**，type 取 `(byte >> 1) & 0x3F`（首字节的中间 6 bit）。把这个函数原样搬到 H.265 解析里是最经典的迁移 bug——type 全错、边界全乱。详见 [§5.4 H.264 → H.265 差异速查](#54-h264--h265hevc差异速查)。

---

## 四、AVCC vs Annex-B：两种切边界方式

第三节已速览两种切法，这里展开细节。核心问题：**怎么区分哪到哪是一个完整的 NALU**？

### 4.1 Annex-B（直播流格式）

每个 NALU 前加一个**起始码**：

```
[startcode][NALU][startcode][NALU][startcode][NALU]...
```

起始码是 `00 00 01`（3 字节）或 `00 00 00 01`（4 字节）。**两种都合法**，解码器扫到 `00 00 01` 就知道新 NALU 开始了（4 字节只是前面多垫了一个 `0x00`）。

#### 起始码：3 字节还是 4 字节？

H.264 标准（Annex B）把起始码拆成两部分理解：

```
[leading_zero × N][zero_byte?][00 00 01][NALU payload]
                      ↑ 多出来的这一个 0x00，就是 3 字节和 4 字节的差别
```

- **3 字节** `00 00 01`：没有 `zero_byte`
- **4 字节** `00 00 00 01`：在 `00 00 01` 前**多一个** `0x00`（即 `zero_byte` 存在）

标准规定 **`zero_byte` 必须出现**（必须用 4 字节起始码）的情况：

| 条件 | 为什么 |
|---|---|
| NALU type 是 **SPS(7) / PPS(8) / SEI(6) / AUD(9)** | 参数集、辅助信息类 NALU 一律 4 字节 |
| type 是 **slice(1 或 5)**，且**上一个 NALU 的最后一个字节是 `0x00`** | 防止 `…00` + `00 00 01` 和 NALU 负载里的防竞争字节 `00 00 03 xx` 边界混淆 |

其余 slice NALU，若上一个 NALU 末尾不是 `0x00`，用 **3 字节** `00 00 01` 即可。

**真实场景里谁用什么？**

| 来源 / 场景 | 常见做法 | 说明 |
|---|---|---|
| **x264 / 多数软编** | 3/4 字节**混用**，按上面规则来 | 省几个字节；抓包能看到同一 GOP 里两种都有 |
| **FFmpeg `h264_mp4toannexb`** | **一律 4 字节** | 实现简单、永远合法；MP4→裸流/TS 转出来的 `.h264` 几乎全是 `00 00 00 01` |
| **Android MediaCodec 编码输出** | 多数 **4 字节** | 硬编直接吐 Annex-B，常见全 4 字节 |
| **TS / HLS 切片、RTP 打包前的裸流** | 两种都能见到 | 取决于上游编码器；下游解析器**必须两种都认** |
| **自己手写 AVCC→Annex-B** | 建议**统一 4 字节** | 永远符合规范；面试/工程上比抠 3 字节省字节更稳 |

**解析器怎么扫？** 不能死板地只认 4 字节。工程上从 `0x01` 往前数连续几个 `0x00`：紧挨 `0x01` 的前两个是 `00 00`，再往前如果还是 `0x00` 就归入起始码（4 字节），否则是 3 字节。NALU **payload 内部**不会出现裸的 `00 00 01`——编码器会插 **emulation prevention byte** `0x03`（`00 00 03 01` 这种），所以"扫起始码切 NALU"在 Annex-B 里是安全的（AVCC 里扫就全乱，见 §5）。

> **面试一句话**：3 字节和 4 字节都合法；SPS/PPS/SEI/AUD 必 4 字节，slice 在上一个 NALU 以 `0x00` 结尾时也要 4 字节；工程转换统一打 4 字节最省事，解析器两种都要支持。

#### 防竞争字节（Emulation Prevention Byte）：万一编码出 `00 00 01` 怎么办

这是个很自然的问题：Annex-B 靠 `00 00 01` 切 NALU 边界，那编码器输出的压缩数据里**万一刚好出现 `00 00 01` 这个字节序列**，解码器不就切错了吗？

H.264 标准用 **防竞争字节（emulation prevention byte）** 解决这个问题，值固定为 `0x03`。

**编码端规则**（插入 `0x03`）：

编码器在写完一个 NALU 的 payload 时，从左到右扫描，遇到以下模式就在第二个 `0x00` 后面插入 `0x03`：

```
00 00 00  →  00 00 03 00
00 00 01  →  00 00 03 01
00 00 02  →  00 00 03 02
00 00 03  →  00 00 03 03
```

一句话：**任何 `00 00` 后面跟了 `[00, 01, 02, 03]` 中任意一个，就在第二个 `00` 后面塞一个 `0x03`**。

#### 为什么 `00`, `01`, `02`, `03` 都要防？

四个字节并不是"顺手全防了"——每一个都有独立的、非防不可的理由。

**`00 00 01` — 直接的 NALU 边界冲突（这是为什么有这个机制）**

Annex-B 靠扫描 `00 00 01` 切 NALU。如果 payload 里自然出现了这个字节序列，解码器就会在数据中间错误地切开一个"假 NALU"，后面全乱。这是防竞争机制存在的**第一原因**——`00 00 01` 必须被破坏掉。

**`00 00 00` — 防止和下一个 NALU 的起始码拼接成 4 字节起始码**

`00 00 00 01` 是 4 字节起始码。假设 payload 尾部恰好是 `...00`，而下一个 NALU 的起始码是 `00 00 01`（3 字节），拼起来就是：

```text
payload 尾 ...00  |  00 00 01 [下一个 NALU]
                  └────┬────┘
                  这四个字节拼成了 00 00 00 01
```

解码器有可能把这个拼接位置误判为 4 字节起始码，切错边界。所以 `00 00 00` 也必须防。

**`00 00 02` — 预留标记，标准制定时提前堵住**

H.264 标准预留了 `00 00 02` 作为某些扩展场景的标记（类似于起始码的变体，比如某些 profile 用它做特殊定界符）。标准在制定时直接把 `0x00` ~ `0x03` 全部纳入防竞争范围，是一种**防御性设计**——与其等未来扩展时出现漏洞再修，不如一开始就全防住。

**`00 00 03` — 最精巧的一个：保证解码端规则"无歧义"**

这是四个里需要多想一步的。解码端的规则只有一句：**看到 `00 00 03`，就把 `03` 丢掉，后面那个字节原样保留**。

但如果编码端**不对 `00 00 03` 做防竞争**，会有致命歧义。假设原始 payload 里恰好有这段：

```text
原始数据:  ... 00 00 03 01 ...
```

解码器扫到 `00 00 03` 时，它无法判断：

- 情况 A：`03` 是编码端插入的防竞争字节，原始数据是 `00 00 01`，应该**丢掉 `03`**。
- 情况 B：`03` 本来就是原始数据的一部分（原始就是 `00 00 03 01`），**不该丢**。

两种情况解码器看到的东西完全一样——`00 00 03 01`。解码器不知道该不该删这个 `03`。**两个不同的原始输入，产生了相同的码流，解码端没办法区分**。

**解决办法**：编码端在扫描时，如果遇到原始数据里的 `00 00 03`，也插一个 `0x03`：

```text
原始数据:  ... 00 00 03 01 ...
编码端:    ... 00 00 03 03 01 ...
                        ↑ 插入的 03
解码端:    ... 00 00 03 01 ...     （丢掉插入的 03，恢复原始）
```

这样一来，解码端可以**无脑执行**同一个规则：凡是 `00 00 03`，`03` 一定是插入的防竞争字节，直接丢掉。**不需要任何额外判断**——因为编码端保证了码流里永远不会出现"裸的" `00 00 03`。

#### 一句话总结四个字节

```text
00 00 01 → 防「假起始码切错 NALU」（机制存在的根本原因）
00 00 00 → 防「和下一个 NALU 起始码拼接成 00 00 00 01」
00 00 02 → 防「预留标记被误读」（防御性设计）
00 00 03 → 防「解码器无法区分真数据和防竞争字节」（保证解码规则无歧义）
```

四个一起防，换来的是**解码端逻辑极简且无歧义**：见到 `00 00 03` 就把 `03` 丢掉。这是典型的"把复杂度推给编码端，让解码端尽可能简单"的协议设计哲学——编码做一次，解码做无数次。

**解码端规则**（移除 `0x03`）：

解码器切出 NALU payload 后，在送给解码内核之前，扫描并移除这些插入的 `0x03`：

```
00 00 03 00  →  00 00 00
00 00 03 01  →  00 00 01
00 00 03 02  →  00 00 02
00 00 03 03  →  00 00 03
```

规则是：**看到 `00 00 03`，就把 `03` 丢掉，后面那个字节原样保留**。

**一个具体例子**：

假设编码器输出的原始 payload 里有这段字节：

```
... 12 34 00 00 01 56 78 ...
```

编码端扫描到 `00 00 01`，插入 `0x03`：

```
... 12 34 00 00 03 01 56 78 ...
                   ↑ 插入的防竞争字节
```

码流里实际存储的是上面这串。解码器扫起始码的时候，遇到的是 `00 00 03 01`，**不会误判成 NALU 边界**（因为没有裸的 `00 00 01`）。切出 NALU 后，解码内核再做一次去防竞争——看到 `00 00 03` 就把 `03` 丢掉——恢复成原始数据 `00 00 01 56 78`。

**关键结论**：

- 防竞争字节只作用于 **NALU payload 内部**，起始码本身不受影响——编码器不会在起始码的 `00 00 01` 里插 `0x03`。
- 这就是为什么 Annex-B 下**扫描起始码切 NALU 是安全的**——payload 里绝不会有裸的 `00 00 01`，碰到的起始码一定是真的 NALU 边界。
- **AVCC（长度前缀）不需要防竞争字节**——它用 4 字节长度字段定位 NALU，不靠扫描起始码，所以 payload 里有裸 `00 00 01` 也完全无所谓。
- H.265 用完全一样的机制，只是起始码固定 4 字节 `00 00 00 01`，防竞争规则不变。

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
| NALU 切割 | 长度前缀 | 起始码 `00 00 01` / `00 00 00 01` |
| SPS / PPS 位置 | `avcC` extradata 集中存放 | 数据流里周期性出现 |
| 长度字段占几字节 | 看 `avcC` 配置（一般 4） | 无 |
| 寻找下一个 NALU | 直接按长度跳过 | 扫描起始码 |
| 典型容器 / 协议 | MP4 / FLV / MKV | RTMP / TS / RTP / 裸流 |

> 实战落地：iOS/Android 硬编解码器在这两种格式间的转换坑见 [10-移动端硬件编解码.md](./10-移动端硬件编解码.md)（VideoToolbox 出 AVCC、MediaCodec 吃 Annex-B）；RTP 打包吃 Annex-B 风格 NALU 见 [08-网络协议与流媒体.md](./08-网络协议与流媒体.md) 的 WebRTC 实时栈。代码里怎么做这一步转换见下面 [§5.3 bsf](#53-bitstream-filterbsf的-api-形态)。

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

### 5.3 Bitstream Filter（bsf）的 API 形态

前面 §5.1 用了 `-bsf:v h264_mp4toannexb` 这条命令。bsf 到底是什么、代码里怎么调？这里补齐。

**bsf 是对"已编码比特流"做轻量变换的过滤器——它不解码、也不重新编码**，只在比特流层面做格式/打包的翻译或裁剪。最常见的就是：

- `h264_mp4toannexb`：把 H.264 从 AVCC（长度前缀）转成 Annex-B（起始码），并从 `avcC` 注入 SPS/PPS。
- `hevc_mp4toannexb`：H.265 的对应物，做同样的事（注意它还要处理 VPS，见 §5.4）。

**反方向（Annex-B → AVCC）通常不需要手动 bsf**：当你把 Annex-B 码流喂给 MP4 muxer 时，muxer 会自动把起始码 NALU 重新打成长度前缀，并把参数集收进 `avcC`。这是封装器的内建职责，写代码时不用自己挂一个反向 bsf。

#### API 三件套的标准骨架

C++ 里 bsf 的生命周期是「分配 → 配置 extradata → 初始化 → 反复送/收 → 释放」。送进去一个 packet，可能收回 0 个、1 个或多个 packet（所以 receive 要用循环）：

```cpp
const AVBitStreamFilter *filterDefinition = av_bsf_get_by_name("h264_mp4toannexb");
if (!filterDefinition) { /* 该 bsf 不存在，报错返回 */ }

AVBSFContext *bsfContext = nullptr;
if (av_bsf_alloc(filterDefinition, &bsfContext) < 0) { /* 分配失败 */ }

// 关键：把输入流的编解码参数(含 avcC extradata)拷给 bsf，
// 否则 mp4toannexb 拿不到 SPS/PPS。inputCodecParameters 来自 AVStream->codecpar
avcodec_parameters_copy(bsfContext->par_in, inputCodecParameters);

if (av_bsf_init(bsfContext) < 0) { /* 初始化失败，记得 free */ }

// ——主循环里，对每个 demux 出来的 packet——
if (av_bsf_send_packet(bsfContext, inputPacket) < 0) { /* 送入失败 */ }

while (true) {
    AVPacket *outputPacket = av_packet_alloc();
    int receiveStatus = av_bsf_receive_packet(bsfContext, outputPacket);
    if (receiveStatus == AVERROR(EAGAIN) || receiveStatus == AVERROR_EOF) {
        av_packet_free(&outputPacket);
        break;                       // 当前没有更多输出，回去送下一个 inputPacket
    }
    if (receiveStatus < 0) { /* 真正的错误，清理退出 */ }

    // outputPacket 现在是 Annex-B，可写裸流文件 / 进 RTP 打包
    av_packet_unref(outputPacket);
    av_packet_free(&outputPacket);
}

// 收尾：送一个 NULL packet 冲刷，最后释放
av_bsf_free(&bsfContext);          // 内部会一并释放 par_in/par_out
```

要点：

- `EAGAIN` 不是错误，只是「这次没攒够输出，继续送」——和解码器的 send/receive 模型完全一致（见 [01-数据结构与生命周期.md](./01-数据结构与生命周期.md) §6.4）。
- 流结束时 `av_bsf_send_packet(bsfContext, nullptr)` 送一个空包冲刷（flush），再把残留 receive 干净。
- `par_in` 的 extradata 一定要正确赋值，这是 `mp4toannexb` 能取出 SPS/PPS 的唯一来源。

#### 常用 bsf 速查

| bsf 名称 | 用途一句话 |
|---|---|
| `h264_mp4toannexb` | H.264：AVCC → Annex-B，并注入 SPS/PPS |
| `hevc_mp4toannexb` | H.265：AVCC → Annex-B，并注入 VPS/SPS/PPS |
| `extract_extradata` | 把内联在码流里的参数集抽出来放到 packet 的 side data |
| `dump_extra` | 反过来，把 extradata 注入到（每个或关键）packet 里 |
| `h264_metadata` / `hevc_metadata` | 不重编码改 SPS/PPS 里的元数据（如 SAR、color 信息） |
| `aac_adtstoasc` | 音频对照物：AAC 从 ADTS（直播）转成 ASC（MP4 用），见 §九 |

**这条路是推流 / WebRTC 的必经路**：从 MP4 demux 出来的 H.264 是 AVCC，而 RTP 打包吃的是 Annex-B 风格的 NALU。中间这一步格式翻译，工程上就是挂一个 `h264_mp4toannexb` bsf 完成的。移动端硬件编解码器侧的同款转换见 [10-移动端硬件编解码.md](./10-移动端硬件编解码.md)。

### 5.4 H.264 → H.265（HEVC）差异速查

打 WebRTC / 推流地基时，迟早会从 H.264 迁到 H.265。两者「比特流如何组织」的思路一脉相承（都有 NALU、参数集、AVCC/Annex-B 两种打包），但若干关键位置不一样，照搬代码会踩坑。

| 维度 | H.264（AVC） | H.265（HEVC） |
|---|---|---|
| 基本编码块 | 宏块（Macroblock）固定 16×16 | CTU（Coding Tree Unit）最大 64×64，可递归四叉树细分 |
| NAL header 长度 | 1 字节 | 2 字节 |
| 取 NALU type 的位运算 | `type = byte & 0x1F`（低 5 bit） | `type = (byte >> 1) & 0x3F`（首字节的中间 6 bit） |
| 参数集 | SPS、PPS | **VPS、SPS、PPS（多了 VPS）** |
| 起始码补齐工具（bsf） | `h264_mp4toannexb` | `hevc_mp4toannexb` |
| 压缩率 | 基准 | 同画质约省 50% 码率 |
| 计算复杂度 | 较低 | 显著更高（编码端尤甚，CTU 划分+更多预测模式） |
| 授权 / 专利 | 相对清晰 | 专利池复杂（多家、分散），商用授权是真实成本 |

#### VPS 是 H.265 多出来的第三层参数集

H.264 只有两层参数集（SPS 描述序列、PPS 描述图像）。**H.265 在它们之上多了 VPS（Video Parameter Set）**，用来描述整个视频的层级信息（多层/可分级编码、多个 SPS 共享的顶层参数）。

这对「参数集补齐」逻辑很关键：§六讲的「每个 IDR 前注入 SPS/PPS」策略，到了 H.265 必须扩展成**注入 VPS + SPS + PPS 三件**。漏掉 VPS，部分解码器会直接解不出来。`hevc_mp4toannexb` 已经帮你处理了这件事，但如果你手写参数集注入逻辑，VPS 是最容易被忘掉的那一个。

#### 迁移提醒

- **解析 NALU type 的位运算不一样**：H.264 是 `byte & 0x1F`，H.265 是 `(byte >> 1) & 0x3F`。把 §三那个 `getNaluType` 直接复制到 H.265 解析里，是最经典的迁移 bug——type 全错、切边界全乱。
- 参数集 type 编号也变了（H.265 里 VPS=32、SPS=33、PPS=34），不能套用 H.264 的 7/8。
- 码控、profile/level 参数的差异见 [06-编码参数与码控.md](./06-编码参数与码控.md)；硬件编解码器对 H.265 的支持情况见 [10-移动端硬件编解码.md](./10-移动端硬件编解码.md)。

> 想系统吃透 H.264/H.265（编码原理、CTU、CABAC、Profile、使用场景与专利授权、面试速记与常考题），看专题 [11-H264与H265详解.md](./11-H264与H265详解.md)——本节只是它的码流结构片段。

> 这两节的自检：bsf 和"解码器"最本质的区别是什么？`av_bsf_receive_packet` 返回 `EAGAIN` 该怎么办？把 Annex-B 的 H.264 写进 MP4 需要手动挂反向 bsf 吗？同一个 `getNaluType(byte){return byte&0x1F;}` 用在 H.265 上会怎样？H.265 比 H.264 多出来的那层参数集叫什么、为什么补齐逻辑必须带上它？

### 5.5 主流封装格式横向对比（MP4 / FLV / TS，高频考点）

§一说过"编码≠封装"。面试常追问**几种容器的结构和取舍**，尤其"为什么直播不用普通 MP4"。这里把 MP4 / FLV / TS 拉到一起讲。

#### MP4：索引集中，适合点播、不适合直播

结构见 §5.2：`moov`（索引）+ `mdat`（数据）。关键性质：

- **`moov` 要等所有 sample 写完才能算出来**（每个 sample 的偏移/大小/时序），所以默认 **`moov` 在文件尾**。
- 网络播放要先拿到 `moov` 才能 seek/起播 → 普通 MP4 得**整段下完或服务器支持 Range**。用 **`-movflags +faststart`** 把 `moov` 挪到文件头，才能边下边播。
- **直播为什么不能用普通 MP4**：直播没有"录完"这一刻，`moov` 永远写不出来。解法是 **fragmented MP4（fMP4）**——把流切成一片片 `moof`（片段索引）+`mdat`，**边录边发**，这正是 HLS/DASH/CMAF 低延迟的容器基础。

#### FLV：流式结构，天生适合直播

FLV 没有"全局索引在尾部"的包袱，是**为流式而生**：

```
FLV Header (9 字节: 'F''L''V' + 版本 + 音/视频标志 + 头长)
└─ Tag, Tag, Tag, ...   (每个 Tag 前后有 PreviousTagSize)
   每个 Tag = [类型(8=音频/9=视频/18=Script)] [数据大小] [时间戳(ms)] [StreamID=0] [数据]
```

- **Script Tag（18）**：装 `onMetaData`——时长、宽高、编码、码率等元信息。
- **第一个 Video Tag**：是 **AVC sequence header**（即 `AVCDecoderConfigurationRecord` / `avcC`，里面是 SPS/PPS），等价于 MP4 的 `avcC`。
- 每个 Tag 自带时间戳、可以来一个发一个 → **天然适合 RTMP/HTTP-FLV 这种"源源不断推"的直播**。这也是 §6.4 说"RTMP 和 HTTP-FLV 内部 FLV Tag 二进制一致、换壳不耗 CPU"的原因。

#### TS（MPEG-TS）：固定小包，抗丢包、可任意切入

为广播/不可靠网络设计：

- **固定 188 字节小包**，每包以同步字 `0x47` 开头，带 **PID** 标识属于哪一路。
- **PAT（PID 0）→ PMT → 各 ES 的 PID**：一套自带的"节目表"，**任意位置切进来都能靠下一个 PAT/PMT 和关键帧重新对齐**——所以 **HLS 把直播切成一个个 `.ts` 文件**、广电 DVB 也用它。
- 媒体放在 **PES** 包里（带 PTS/DTS）。
- 代价：188 字节固定头**开销比 MP4/FLV 大**。

#### 一句话对比表

| 容器 | 索引方式 | 适合 | 为什么 |
|---|---|---|---|
| **MP4** | `moov` 集中（默认在尾） | 点播存储 | 索引全、seek 准；直播要用 fMP4 变体 |
| **fMP4** | 分片 `moof` | HLS/DASH 低延迟、CMAF | 边录边发，无需等录完 |
| **FLV** | 无全局索引，Tag 流 | RTMP/HTTP-FLV 直播 | 流式结构，来一片发一片 |
| **TS** | PAT/PMT + 固定小包 | HLS 切片、广电 | 抗丢包、任意切入，代价是头开销大 |

**最高频的那道题**——"为什么直播用 FLV/TS 不用 MP4？"：因为普通 MP4 的 `moov` 索引要等录完才能写，直播没有"录完"；FLV/TS 是流式结构，能边产边发。要用 MP4 系就得上 fMP4。

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

判断方法要分清两层：`nal_ref_idc == 0` 表示该 NALU **不被任何后续帧参考**（即"可丢"），这是裸 NAL header 就能读到的。但**它判断不出分层 B 的层级**（谁参考谁）——可靠的层级信息要靠 SVC 扩展的 prefix NAL / `temporal_id`，或编码器的 B 帧金字塔配置。所以工程上的弱网降帧:先用 `nal_ref_idc==0` 安全地丢"叶子帧",要做精细的分层丢弃才需要 temporal_id。**丢错了直接花屏**。

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

### 7.4 编码器内部：视频为什么能压这么小（开放题必备）

面试常问一道开放题："**讲讲 H.264 是怎么把视频压缩这么小的？**" 答得清楚 = 你真懂编解码而不只会调 API。核心一句话：**编码就是消除四种冗余**。

| 冗余 | 怎么消 | 对应技术 |
|---|---|---|
| **空间冗余**（一帧内相邻像素相似） | 用同帧已编码的相邻像素预测当前块，只编差值 | **帧内预测**（I 帧，§7.1） |
| **时间冗余**（相邻帧大部分没变） | 在参考帧里找最像的块，只编"运动矢量 + 残差" | **帧间预测 + 运动估计/补偿**（P/B 帧） |
| **视觉冗余**（人眼对高频细节/色度不敏感） | 变换到频域后，量化时把高频狠狠丢掉 | **变换 + 量化**（有损就发生在这一步） |
| **统计冗余**（符号出现概率不均） | 高频符号用短码、低频用长码 | **熵编码**（CABAC / CAVLC） |

完整编码流水线（解码端反着来）：

```
原始块 → 预测(帧内/帧间) → 残差 → 变换(DCT/整数变换) → 量化 → 熵编码 → 码流
                                              ↑
                                    质量/码率的旋钮就在这(QP/CRF,见 06)
                                    解码端还有 环路滤波(deblocking) 去块效应
```

几个面试加分点：

- **质量损失只发生在"量化"这一步**——QP/CRF 调的就是量化粗细（见 [06-编码参数与码控.md](./06-编码参数与码控.md)）。预测、变换、熵编码本身不丢信息。
- **熵编码两种**：CAVLC（简单、快，Baseline Profile）vs **CABAC**（算术编码，压缩率更高但更耗算力，Main/High Profile）。这正好解释了 §06 的 Profile 差异。
- **环路滤波（in-loop deblocking）**：块状压缩会在块边界产生"马赛克边"，编码器在重建帧上做去块滤波再当参考帧——所以参考帧质量更高、后续预测更准。
- **H.265 的进化**：更大更灵活的编码块（CTU 四叉树，见 §5.4）、更多帧内预测方向、只用 CABAC、多了 SAO（样点自适应补偿）等——同画质省约 50% 码率，代价是编码算力大增。
- **音频（AAC）同理**：靠**心理声学模型**砍掉人耳听不到的频率分量（频域 + 量化），再熵编码——和视频"消除视觉冗余"是一个思路，只是换成"听觉冗余"。

> 一句话收尾模板："**预测去掉空间和时间冗余、变换+量化利用人眼不敏感丢高频（有损就在量化）、熵编码再压一道；解码反过来并做环路滤波去块。质量旋钮是量化步长 QP。**"

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

无 B 帧时解码顺序==显示顺序、`DTS` 单调递增（数值不一定等于 `PTS`，可能差个固定起始偏移）；**有 B 帧时 `PTS ≠ DTS`，解码器要按 DTS 顺序解码，渲染器按 PTS 顺序显示**。

> MP4 里 `DTS` 由 `stts` 表（每帧解码时长）累加得到，`PTS` 则靠 `ctts` 表记录的 **composition offset（PTS − DTS 偏移）** 加回去。所以"有 B 帧的 MP4"必然带 `ctts` box；裸流转 MP4 时这套时序表由 muxer 构建（见 §10.2）。

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

> **AAC 的几个 profile（面试会问）**：**AAC-LC**（Low Complexity，最主流，绝大多数 MP4/直播用它）；**HE-AAC**（= LC + SBR 频带复制，低码率下音质更好，适合 ~64kbps 以下）；**HE-AACv2**（= HE-AAC + PS 参数立体声，更低码率的立体声）。码率从高到低选：高码率/音乐用 LC，低码率语音/广播用 HE/HEv2。和实时互动场景默认用 Opus 的对比见 [04-音频PCM-采样-重采样.md](./04-音频PCM-采样-重采样.md) 的 "AAC vs Opus"。

### 9.2 ADTS 给每帧穿一件 7 字节马甲

ADTS Header 7 字节（带 CRC 校验时为 9 字节），包含：

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

> **VFR vs CFR 的坑**：上面按固定 framerate 重封装假设的是 **CFR（恒定帧率）**。但手机录制、屏幕录制、很多 WebRTC 采集是 **VFR（可变帧率）**——每帧间隔不等。对 VFR 源强行套一个固定 framerate 会导致音画不同步、整体变速。处理 VFR 要保留每帧真实 PTS（用 `-vsync vfr` / 透传时间戳），而不是用单一帧率重新生成时间轴。

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
3. Annex-B 里 `00 00 01` 和 `00 00 00 01` 分别在什么场景用？自己写转换代码时建议用哪种？
4. 把 MP4 里的字节直接喂给 Android MediaCodec 会怎样？为什么？怎么解决？
5. SPS / PPS 通常存在哪里？为什么直播场景要每个 IDR 前都注入？
6. I 帧和 IDR 帧的区别是什么？为什么 seek 只能跳到 IDR？
7. 普通 B 帧和参考 B 帧弱网时哪个能丢？怎么判断？
8. 为什么会有 PTS 和 DTS 两个时间戳？什么时候相等？
9. GOP 长度对首屏秒开和压缩率分别有什么影响？
10. AAC 在 MP4 里和在 TS 直播流里的格式分别是什么？为什么？
11. `ffmpeg -c copy` 到底做了什么？什么时候不能用？
---

## 十一、SPS 字节级解析：从码流里读出宽高

SPS（Sequence Parameter Set）保存的是一段 H.264 序列级参数，例如 profile、level、编码宽高、裁剪信息、VUI 等。面试不一定要求手写完整解析器，但要知道“宽高不是只看容器，也能从 SPS 里读”。

简化流程：

```text
Annex-B / AVCC 中拿到 SPS NALU
  -> 去掉 start code 或 length prefix
  -> 去掉 NALU header
  -> 处理 emulation prevention byte：00 00 03 -> 00 00
  -> 按 bit 读 profile_idc / level_idc / seq_parameter_set_id
  -> 读 pic_width_in_mbs_minus1 / pic_height_in_map_units_minus1
  -> 结合 frame_mbs_only_flag 和 crop 计算真实宽高
```

核心公式：

```text
coded_width  = (pic_width_in_mbs_minus1 + 1) * 16
coded_height = (pic_height_in_map_units_minus1 + 1) * 16 * (2 - frame_mbs_only_flag)
visible_width/height = coded size - crop offsets
```

常见坑：

| 坑 | 说明 |
|---|---|
| 忘记去 `00 00 03` | bit 读取会错位，后面所有字段都错 |
| 把 coded size 当显示尺寸 | H.264 按宏块对齐，真实显示尺寸可能靠 crop 修正 |
| 只相信容器宽高 | 裸流、推流、硬解初始化时经常要靠 SPS/PPS |
| AVCC/Annex-B 混淆 | MP4 里 SPS/PPS 常在 extradata，直播流常在 IDR 前重复出现 |

完整手写解析不如理解链路重要：SPS 是硬解初始化、推流 sequence header、播放器首帧黑屏排查的关键参数来源。
