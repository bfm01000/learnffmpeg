# 04 - 音频 PCM、采样格式与重采样

> 对应导读第 3.3 节"解释问题"、第 3.4 节"节奏问题"、第 6.4 节"Planar vs Packed"。
> 这一篇覆盖 PCM 的全部概念栈：什么是 PCM、采样率、位深、声道布局、Planar/Packed、平台格式偏好、重采样（SwrContext）、音频帧时长计算、音视频同步、缓冲区、AAC 编码、采集端 3A 处理、网络抖动。
> 这是 ffmpeg 笔记里篇幅最长的一篇，因为音频比视频更容易在底层翻车：只要五个参数里有一个错，立刻就是杂音、爆音、卡顿、变速、不同步。

---

## 一、音频处理的全景链路

播放链路：

```
压缩音频包 (AAC/MP3/Opus)
   |  demux
   v
解码 (AAC/MP3/Opus decoder)
   |
   v
PCM 原始数据 (解码器自然输出的格式,例如 FLTP)
   |  格式转换 / 重采样 / 声道转换
   v
设备期望的 PCM (例如 S16 packed 48kHz stereo)
   |
   v
音频回调 / 系统缓冲区
   |
   v
声卡 / 扬声器
```

采集链路：

```
麦克风
   |  系统采集 API
   v
PCM 原始数据
   |  AEC / NS / AGC / VAD
   v
处理后的 PCM
   |  重采样到编码器要求
   v
编码 (AAC / Opus / G.711)
   |
   v
压缩包 -> 封装 / 网络发送
```

面试里最常被问的"杂音、变速、音画不同步、延迟高、爆音"全部能映射回这两张图里某个环节。

---

## 二、PCM 是什么

**PCM（Pulse Code Modulation，脉冲编码调制）**是最常见的原始音频格式。它**不是压缩格式**，而是把连续的声波按固定时间间隔采样，每个采样点量化成数字。

```
声波 -> 每秒取很多个点 -> 每个点用一个数字表示振幅 -> 这些数字连续排列在内存里
```

类比关系：视频解码后是 YUV / RGB，**音频解码后是 PCM**。

### PCM 不能"独立听懂"

PCM 数据本身只是一串数字。要正确播放，必须同时知道**五个参数**：

| 参数 | 含义 | 例 |
|---|---|---|
| 采样率 | 每秒采多少个采样点 | 44100、48000 |
| 采样格式 | 每个采样点怎么表示 | `S16`、`FLT`、`FLTP` |
| 声道数 | 几个通道 | 1（单声道）、2（立体声）、6（5.1） |
| 声道布局 | 每个通道的空间位置 | stereo = FL+FR；5.1 = FL+FR+FC+LFE+BL+BR |
| 内存排列 | Planar 还是 Packed | 见下文 |

**同一段字节，按不同参数解释，会变成完全不同的声音**。这是音频 bug 的根源。

---

## 三、采样率：时间轴的密度

每秒采集多少个采样点，单位 Hz。

| 采样率 | 典型场景 |
|---|---|
| 8000 | 电话窄带语音 |
| 16000 | 语音识别、VoIP |
| 44100 | CD 标准 |
| 48000 | 视频、直播、移动端、专业音频（最常见） |
| 96000 / 192000 | 专业录音、高解析音频 |

### 视频场景为什么用 48000 而不是 44100

44100 来自 CD 历史，48000 在视频和广播里更普及。大多数音频设备硬件原生采样率就是 48k。如果采集、处理、播放、编码统一为 48k，可以减少重采样次数，降低延迟和失真。

### 采样率错配的表现

| 错配方式 | 听感 |
|---|---|
| 44100 PCM 按 48000 播放 | 变快、音调变高、时长变短 |
| 48000 PCM 按 44100 播放 | 变慢、音调变低、时长变长 |

这不是"音频损坏"，是**时间轴解释错了**。修复方法是重采样，不是改字节。

---

## 四、位深与采样格式

每个采样点用多少 bit 表示振幅：

| 格式 | 含义 | 范围 | 典型用途 |
|---|---|---|---|
| `U8` | 8 位无符号 | 0 ~ 255 | 极老格式，几乎绝迹 |
| `S16` | 16 位有符号 | -32768 ~ 32767 | 设备播放主力（CD 音质） |
| `S32` | 32 位有符号 | -2³¹ ~ 2³¹-1 | 高解析音频 |
| `FLT` | 32 位 float | 通常 -1.0 ~ 1.0 | 算法处理 |
| `FLTP` | 32 位 float + Planar | 同上 | **AAC 解码默认输出** |
| `DBL` | 64 位 double | | 专业音频后处理 |

### 为什么音频通常用有符号

声波围绕 0 上下振动，正负值表示波形的两个方向。所以 16 位及以上的 PCM 都是有符号。

### S16 和 FLT 的分工

**S16**：

- 硬件兼容性最好
- SDL、Android AudioTrack、Windows DirectSound 默认支持
- 工程播放链路最常见

**FLT / FLTP**：

- 算法处理友好（混音、滤波、增益不容易溢出）
- AAC 编码器输入、解码器输出常见
- 处理完后通常要转回 S16 再播放

### 爆音的来源：削波（Clipping）

当样本值超过格式可表示范围，就被截断成边界值，波形顶部被削平：

```cpp
// 错误：两个 S16 直接相加,可能溢出
int16_t out = sample1 + sample2;

// 正确：先升到 int 域算,clip 后再降回 S16
int mixed = static_cast<int>(sample1) + static_cast<int>(sample2);
mixed = std::max(-32768, std::min(32767, mixed));
int16_t out = static_cast<int16_t>(mixed);
```

更复杂的混音建议**先转 float、在 float 域处理、再转回目标格式**——float 不会溢出，最后再做 limiter。

---

## 五、声道数 vs 声道布局

声道数只告诉你有几条通道；**声道布局告诉你每条通道的空间含义**。

例：

- 2 声道通常是 `FL + FR`（前左 + 前右，stereo）
- 6 声道可能是 5.1：`FL + FR + FC + LFE + BL + BR`

只知道"6 声道"而不知道布局，播放器不知道每个通道送到哪个扬声器，多声道音乐听起来位置全错。

### FFmpeg 新旧 API

旧版本：

```cpp
uint64_t channelLayout = AV_CH_LAYOUT_STEREO;
```

新版本（推荐）：

```cpp
AVChannelLayout chLayout;
av_channel_layout_default(&chLayout, 2);
// ... 使用 ...
av_channel_layout_uninit(&chLayout);
```

新 API 用 `AVChannelLayout` 结构体表达，明确支持复杂布局和自定义通道。注意它是**栈变量风格**——用 `uninit` 清理（参考 [01-数据结构与生命周期.md](./01-数据结构与生命周期.md) 第 5.3 节）。

不要直接结构体赋值：

```cpp
// ❌ 不安全,内部资源可能共享导致重复释放
encCtx->ch_layout = outChLayout;

// ✅ 使用专用 copy
av_channel_layout_copy(&encCtx->ch_layout, &outChLayout);
```

### 常见声道问题

| 现象 | 可能原因 |
|---|---|
| 只有左声道有声音 | 数据只取了 `data[0]`（Planar 时遗漏了 `data[1]`） |
| 左右声道反 | NV21 / NV12 那种顺序搞反 |
| 5.1 转双声道发闷 | downmix 权重不合理 |
| 单声道当立体声播 | 步进错乱、字节数算错 |

---

## 六、Planar vs Packed：杂音最常见来源

详见 [02-像素格式与内存布局.md](./02-像素格式与内存布局.md) 第二节。这里只对照音频补一遍：

**Packed（交错）双声道**：

```
data[0]:  L0 R0 L1 R1 L2 R2 L3 R3 ...
```

代表：`AV_SAMPLE_FMT_S16`、`AV_SAMPLE_FMT_FLT`。声卡爱这种格式。

**Planar（平面）双声道**：

```
data[0]:  L0 L1 L2 L3 ...   <- 左声道独占
data[1]:  R0 R1 R2 R3 ...   <- 右声道独占
```

代表：`AV_SAMPLE_FMT_S16P`、`AV_SAMPLE_FMT_FLTP`。编码器和算法爱这种格式。

### 为什么 FFmpeg 解 AAC 默认输出 FLTP

AAC 等现代音频编码在内部做频域变换、心理声学分析、单声道独立处理。**浮点 + 平面**对这些计算最友好——左声道算法只读 `data[0]`，右声道只读 `data[1]`，互不干扰，且 float 不会溢出。

但很多设备和库期望 `S16` packed。直接把 `FLTP` 当成 `S16` 喂给 SDL，**几乎必然是刺耳杂音**——数值类型和声道排列同时全错。

---

## 七、平台与库的格式偏好

| 场景 | 期望格式 | 备注 |
|---|---|---|
| FFmpeg 解 AAC | `AV_SAMPLE_FMT_FLTP` | 解码器默认输出 |
| SDL2 播放 | `AUDIO_S16SYS` 即 `AV_SAMPLE_FMT_S16` | Packed 交错 |
| Android AudioTrack | `ENCODING_PCM_16BIT` 即 `S16` | Packed 交错 |
| iOS / macOS AudioUnit | 通常 `FLT` 或 `FLTP` | 苹果原生爱浮点，可省一次转换 |
| FFmpeg AAC 编码器 | `FLTP` 或 `S16` | 看具体 codec 的 `sample_fmts` 列表 |

**SDL2 是什么**：跨平台多媒体库 Simple DirectMedia Layer。常用作 PCM 播放出口——FFmpeg 解码，SDL2 把 PCM 喂给系统声卡。两者分工：FFmpeg 解决"把压缩音频解出来"，SDL2 解决"把裸 PCM 播出来"。

**典型 bug 流程**：

```
MP4 文件
  -> FFmpeg 解码 AAC -> AVFrame(FLTP, 44100Hz, stereo)
  -> 直接 memcpy 给 SDL2 (S16, 48000Hz) -> 刺耳杂音
```

刺耳的根因：

1. 数值类型错（float 当成 int16 读）
2. 采样率错（44100 当成 48000 播）
3. 声道排列错（planar 当成 packed 读）

修复方法：插入 `SwrContext` 做完整重采样（采样率 + 格式 + 排列 + 声道）。

---

## 八、SwrContext：不只是改采样率

`libswresample` 能同时处理 5 件事：

- **采样率转换**：44100 → 48000
- **采样格式转换**：FLTP → S16
- **声道数转换**：mono → stereo
- **声道布局转换**：5.1 → stereo（downmix）
- **Planar / Packed 转换**

工程上虽然都叫"重采样"，实际是一组音频格式归一化操作。

### 必须重采样的典型场景

- 解码器输出格式 ≠ 播放器输入格式
- 麦克风采集格式 ≠ 编码器输入格式
- 多路音频混音前采样率不一致
- 直播协议要求固定采样率（如 48k）
- 跨平台时不同音频设备的格式约束

### 完整 C++ 流程

```cpp
extern "C" {
    #include <libswresample/swresample.h>
    #include <libavutil/samplefmt.h>
    #include <libavutil/channel_layout.h>
}

SwrContext* swr = nullptr;

// 新版本推荐 swr_alloc_set_opts2
AVChannelLayout outChLayout;
av_channel_layout_default(&outChLayout, 2);  // stereo

int ret = swr_alloc_set_opts2(
    &swr,
    &outChLayout, AV_SAMPLE_FMT_S16, 48000,       // 输出
    &srcFrame->ch_layout, static_cast<AVSampleFormat>(srcFrame->format), srcFrame->sample_rate,
    0, nullptr
);

// 每一步都要判错并清理已分配的资源,别用空的 /* error */ 占位
if (ret < 0 || !swr) {
    av_channel_layout_uninit(&outChLayout);
    return false;
}
if (swr_init(swr) < 0) {
    swr_free(&swr);
    av_channel_layout_uninit(&outChLayout);
    return false;
}

// 计算输出 buffer 大小(关键)
int64_t delay = swr_get_delay(swr, srcFrame->sample_rate);
int outSampleCount = av_rescale_rnd(
    delay + srcFrame->nb_samples,
    48000,
    srcFrame->sample_rate,
    AV_ROUND_UP
);

uint8_t** outData = nullptr;
int outLinesize = 0;
if (av_samples_alloc_array_and_samples(
        &outData, &outLinesize,
        2, outSampleCount, AV_SAMPLE_FMT_S16, 0) < 0) {
    swr_free(&swr);
    av_channel_layout_uninit(&outChLayout);
    return false;
}

int converted = swr_convert(
    swr,
    outData, outSampleCount,
    const_cast<const uint8_t**>(srcFrame->extended_data),
    srcFrame->nb_samples
);
// converted < 0 是错误; >= 0 时是每个声道实际输出的样本数
if (converted < 0) { /* 处理转换错误 */ }

// 退出:无论成功失败,已分配的都要释放
av_freep(&outData[0]);
av_freep(&outData);
av_channel_layout_uninit(&outChLayout);
swr_free(&swr);
```

### 输出 buffer 大小为什么不能简单等比例算

重采样器内部有缓存和滤波延迟。输出样本数**不能写成**：

```
out_samples = in_samples × out_rate / in_rate
```

正确公式必须加上重采样器内部延迟：

```
out_samples = (swr_get_delay + in_nb_samples) × out_rate / in_rate (向上取整)
```

否则 buffer 不够，会丢尾音或偶发爆音。

---

## 九、音频帧时长 / 样本数 / 字节数

### 音频里的"一帧"

视频一帧是一张图。**音频一帧是一组连续采样点**，不是一个采样点。

`AVFrame` 里的关键字段：

| 字段 | 含义 |
|---|---|
| `nb_samples` | 每个声道的样本数（不是总样本数） |
| `sample_rate` | 采样率 |
| `format` | 采样格式 |
| `ch_layout` | 声道布局 |
| `data` / `extended_data` | 数据指针 |

时长公式：

```
duration_seconds = nb_samples / sample_rate
```

例：`nb_samples = 1024, sample_rate = 48000` → 21.33 ms。

### AAC 为什么常见 1024

AAC-LC 一帧固定 1024 个样本。48k 下一帧约 21.33ms，44.1k 下约 23.22ms。

这也是音频发送、同步、网络分包常见的时间粒度。

### PCM 字节数计算

Packed：

```
bytes = nb_samples × channels × bytes_per_sample
```

例：48k、双声道、S16、10ms：

```
samples = 48000 × 0.01 = 480
bytes = 480 × 2 × 2 = 1920 bytes
```

Planar：

```
每个声道平面大小 = nb_samples × bytes_per_sample
总大小 = 每个声道平面大小 × channels
```

更稳妥的写法是直接用工具函数：

```cpp
int size = av_samples_get_buffer_size(
    nullptr,
    channels,
    nbSamples,
    sampleFmt,
    1   // align
);
```

### linesize 同样要小心

音频 `AVFrame->linesize[0]` 表示一个音频平面的字节数。**它可能因为内存对齐而大于理论值**。不要假设：

```
linesize[0] == nb_samples × bytes_per_sample
```

按 `linesize` 或 `av_samples_get_buffer_size` 处理。

---

## 十、时间戳与音视频同步

### 音频 PTS 的特殊性

音频 PTS 表示这一帧应该从什么时间开始播放。一帧持续时长由 `nb_samples / sample_rate` 决定。

```cpp
double seconds = pts * av_q2d(time_base);
```

### 为什么以音频为主时钟

主流播放器（FFplay / VLC / mpv）都用**音频时钟当主时钟**，视频追音频。原因：

- 人耳对音频卡顿和变速极其敏感（耳朵秒辨"声音卡了"）
- 人眼对视频丢一两帧不敏感（视觉残留 + 大脑脑补）

调整音频速度会引入变调或杂音，代价远大于"视频偶尔丢一帧"。

### 音频时钟的精确计算

理想公式：

```
audio_clock = 当前正在播放的音频帧 PTS + 已被设备真正播放掉的样本时长
```

但**写入系统缓冲区 ≠ 已被听到**。系统 buffer 里可能还有几十到几百毫秒的数据。精确时钟要考虑：

- 已提交给设备的总样本数
- 设备实际消耗进度（如果系统 API 能查）
- 当前缓冲区剩余时长

### 同步策略

```
if 视频 PTS < 音频时钟 - 阈值 :  视频落后,丢帧追赶
if 视频 PTS > 音频时钟 + 阈值 :  视频超前,延迟显示
else :                            正常显示
```

一般阈值在几十毫秒。**不频繁拉伸音频追视频**——除非特殊需求，否则音频自由跑、视频跟。

### Seek 后为什么容易不同步

seek 后旧的 packet queue / frame queue / 重采样缓存 / 解码器内部缓存还残留旧数据。如果不 flush 干净，会播到 seek 之前的内容。正解：

1. 清空 packet queue 和 frame queue
2. 调用 `avcodec_flush_buffers` 让解码器清掉内部缓存
3. 重置 `SwrContext`（如果有连续状态）
4. 重置 audio clock

---

## 十一、缓冲区、延迟与卡顿

### 为什么音频必须有缓冲区

声卡按固定节奏消费 PCM。应用层线程不可能完美匹配这个节奏。中间需要缓冲区做削峰填谷。

| 缓冲区大小 | 延迟 | 稳定性 |
|---|---|---|
| 太小 | 低 | 差，CPU 调度抖动就 underrun |
| 太大 | 高 | 好，但直播 / 通话体验差 |

音频开发永远在这两个轴上取平衡。

### Underrun 和 Overrun

- **Underrun**：设备要数据，应用没准备好。听感是**断音、卡顿**。
- **Overrun**：采集或网络数据来得太快，应用消费不及。后果是**缓冲区堆积、延迟升高、最终丢数据**。

### 音频回调里千万不要做的事

音频系统的回调线程通常是高优先级实时线程。在里面绝对不要：

- 阻塞等待锁
- 磁盘 / 网络 IO
- 复杂解码
- 动态内存分配
- 打日志

推荐架构：

```
解码线程 -> 生产 PCM -> 写入环形缓冲区
                                  |
                                  v
                          音频回调 (只读 buffer + memcpy)
```

环形缓冲区是这种生产者-消费者模型的标配。

---

## 十二、AAC 编码与 FIFO

### 编码器不是想喂多少喂多少

很多音频编码器有固定 `frame_size`。**AAC-LC 必须每次喂 1024 个样本**。

如果采集端每次给你的是 10ms（480 samples @ 48k），而 AAC 要 1024，你得自己攒：

```
采集 480 -> FIFO  (累计 480)
采集 480 -> FIFO  (累计 960)
采集 480 -> FIFO  (累计 1440)
取 1024 编码  -> 剩余 416
采集 480 -> FIFO  (累计 896)
采集 480 -> FIFO  (累计 1376)
取 1024 编码  -> 剩余 352
...
```

FFmpeg 提供 `AVAudioFifo` 专门做这件事：

```cpp
extern "C" {
    #include <libavutil/audio_fifo.h>
}

AVAudioFifo* fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, 2, 1024);

// 写入
av_audio_fifo_write(fifo, (void**)frame->extended_data, frame->nb_samples);

// 读出 1024 个样本编码
while (av_audio_fifo_size(fifo) >= 1024) {
    av_audio_fifo_read(fifo, (void**)encodeBuf, 1024);
    avcodec_send_frame(encCtx, encFrame);
    // ...
}

av_audio_fifo_free(fifo);
```

### 编码器输入格式不能猜

编码器自己声明支持的格式：

```cpp
codec->sample_fmts  // 一个数组,以 AV_SAMPLE_FMT_NONE 结尾
```

很多人默认 AAC 编码器吃 `S16`，实际不一定——FFmpeg 内置的 AAC 编码器经常要求 `FLTP`。**写代码前先查 `sample_fmts` 数组**，发现不匹配就插一个 SwrContext。

---

## 十三、采集端：AEC / NS / AGC / VAD

实时音视频采集不是简单读麦克风。常见处理链：

| 缩写 | 全称 | 作用 |
|---|---|---|
| AEC | Acoustic Echo Cancellation | 回声消除 |
| NS | Noise Suppression | 噪声抑制 |
| AGC | Automatic Gain Control | 自动增益 |
| VAD | Voice Activity Detection | 语音活动检测 |

### 回声是怎么来的

通话时扬声器播远端声音，这个声音被本地麦克风采到，又传回远端。远端听到自己的回声。

AEC 同时需要：

- 本地麦克风采集信号
- 远端播放参考信号
- 播放到采集的延迟估计

延迟估计不准，回声消除效果会差很多。

### 为什么 WebRTC 的音频处理这么强

Google 的 WebRTC 在音频模块上深耕了十几年，内置成熟的 AEC / NS / AGC / VAD / Jitter Buffer / NetEQ。很多实时音视频项目会**直接复用 WebRTC 的音频处理模块**，而不是从零写。

---

## 十四、网络音频：抖动与丢包

实时音频每 10/20ms 发一个包。但网络到达时间往往是：

```
20ms, 20ms, 60ms, 5ms, 15ms, 80ms ...
```

直接按到包时间播放会严重卡顿。

### Jitter Buffer

把不均匀到达的网络包缓存并按 RTP 时间戳重排，再按稳定节奏交给解码器。它处理：

- 乱序
- 丢包检测
- PLC 丢包隐藏
- 动态延迟（在抗抖动和低延迟间平衡）

太小抗不住抖动，太大增加通话延迟。Jitter Buffer 的策略调优是 RTC 工程的核心难点之一。

### PLC（Packet Loss Concealment）

丢包时根据前后音频估算一段"假数据"填上，避免直接静音或爆音。**它不能真正恢复丢失内容**——只是听感上更自然。

语音场景短时间 PLC 效果不错；音乐场景容易被听出来。

### AAC vs Opus（RTC 视角）

前面整篇都在讲 AAC + ADTS + `AVAudioFifo`,那是**点播 / 直播存档**的世界。但只要你转头做实时互动(WebRTC、语音通话),会发现默认编码器根本不是 AAC,而是 **Opus**。原因不是"Opus 音质更好",而是它从设计目标上就是为实时网络生的。

#### 为什么 RTC 选 Opus 而不是 AAC

三个互相独立的理由:

- **低延迟**。AAC-LC 一帧固定 1024 样本(48k 下约 21.33ms,见第九节),这个时间粒度对点播无所谓,对通话却是硬延迟。Opus 帧长可以低到 2.5ms,编码器算法延迟也远小于 AAC。
- **抗丢包**。Opus 把丢包隐藏(PLC)和前向纠错(FEC)做进了编码器内部,而 AAC 裸流丢一包就是一段静音或爆音,需要外层自己兜底。
- **免版税**。Opus 是开放标准(IETF RFC 6716),没有专利授权费;AAC 的专利授权在很多商业场景是真实成本。WebRTC 作为浏览器标准必须用免版税编码器,这一条几乎是决定性的。

#### 可变帧长 vs 固定 1024 样本:延迟从哪来

这是 AAC 和 Opus 最本质的差别。

AAC-LC **每帧锁死 1024 样本**——所以你才需要 `AVAudioFifo` 把采集端的 10ms 小块攒够 1024 再编码(第十二节)。攒包这个动作本身就引入延迟:采集端给你 480 样本,你必须等到凑满 1024 才能编一帧。

Opus 帧长**可选 2.5 / 5 / 10 / 20 / 40 / 60 毫秒**,默认 20ms。这意味着:

- 你想要更低延迟,就用更短帧长(代价是包头开销占比变高、压缩率略降)。
- 采集端按 20ms 出数据,编码器就吃 20ms,**通常不需要 FIFO 攒包**——帧长直接对齐采集粒度。

一句话直觉:AAC 是"先凑够一帧的料再下锅",Opus 是"采集多少就近编多少"。后者天然适合通话这种"每一毫秒都要省"的场景。

#### 内建 PLC 与 in-band FEC:和 Jitter Buffer 配合

本节前面讲过 Jitter Buffer 和 PLC——那里的 PLC 是个通用概念,而 **Opus 把 PLC 和 FEC 直接做进了编码器**,这是它和 AAC 在抗丢包上的代际差距:

- **PLC(丢包隐藏)**:某个包没到,Opus 解码器根据前后音频估算一段过渡数据填上,避免直接静音或爆音。和前面说的一样,它**不恢复真实内容**,只是听感更自然,语音场景效果好、音乐场景容易露馅。
- **in-band FEC(前向纠错)**:更进一步,Opus 可以把**前一帧的低码率冗余副本**塞进当前帧。这样丢了第 N 包,只要第 N+1 包到了,解码器就能用里面的冗余信息**部分重建**第 N 包,而不是靠猜。代价是码率上升。

这两者都和 Jitter Buffer 强相关:Jitter Buffer 负责"重排 + 决定等多久",而当它判定某个包确实丢了、不再等待时,就交给 Opus 的 PLC / FEC 来填补缺口。三者是一条流水线,不是三个互斥方案。

#### 封装:Opus 不需要 ADTS

第十二节强调过 AAC 裸流必须套 **ADTS** 头才能被识别(每帧前面挂一个带采样率、声道、帧长的小头部),否则就是一段无法自描述的字节。

Opus 在 WebRTC 里**不需要这种外层封装**——它**直接由 RTP 承载**:一个 RTP 包通常就装一个 Opus 帧,采样率 / 声道 / 帧长等信息通过信令(SDP)在建连时就协商好了,不必逐帧重复携带。

| 场景 | AAC 的外层 | Opus 的外层 |
|---|---|---|
| 实时传输 | 需要 ADTS / LATM 等封装 | 直接进 RTP 负载 |
| 存进文件 | MP4 / TS 容器 | Ogg / WebM 容器 |
| 自描述性 | 裸流必须靠 ADTS 头 | 靠 SDP 信令协商一次 |

(封装 / 裸流 / Annex-B 这类"裸流为什么需要外层头"的视频侧对照,可参考 [05-H264-MP4-NALU.md](./05-H264-MP4-NALU.md) 的 AVCC vs Annex-B,以及 RTP 承载见 [08-网络协议与流媒体.md](./08-网络协议与流媒体.md) §十。)

#### 频带与采样率:一个编码器通吃语音和音乐

AAC 偏"音乐 / 高码率 / 高采样率"的世界,本质是个面向**存储和高保真回放**的格式。

Opus 的特别之处是**频带自适应**:同一个编码器,根据目标码率和内容,在以下几档之间自动切换——

- 窄带 NarrowBand(8kHz):电话级语音
- 宽带 WideBand(16kHz):清晰语音
- 超宽带 SuperWideBand(24kHz)
- 全频带 FullBand(48kHz):音乐 / 高保真

它内部其实融合了一个语音编码器(SILK)和一个通用音频编码器(CELT),会根据内容在"偏语音"和"偏音乐"之间平滑过渡。所以一路通话里时而说话、时而放音乐,**不用切编码器**,Opus 自己适配。这正是 RTC 需要的:你没法预设用户传的是人声还是背景音乐。

#### 对照表

| 维度 | AAC-LC | Opus |
|---|---|---|
| 设计目标 | 存储 / 回放 / 高保真 | 实时互动 / 低延迟 |
| 帧长 | 固定 1024 样本(48k≈21.33ms) | 可变 2.5/5/10/20/40/60ms(默认 20) |
| 编码延迟 | 较高(还需 FIFO 攒包) | 低,帧长可对齐采集粒度 |
| 抗丢包 | 无内建,靠外层兜底 | 内建 PLC + in-band FEC |
| 实时封装 | 必须 ADTS / LATM | 直接进 RTP,SDP 协商一次 |
| 采样率 / 频带 | 偏高采样率、音乐 | 8k–48k 自适应(NB/WB/SWB/FB) |
| 语音 vs 音乐 | 偏音乐 | 语音 + 音乐通吃(SILK+CELT) |
| 版税 | 有专利授权成本 | 免版税(IETF 开放标准) |
| 典型出口 | MP4 / TS / 直播 CDN | WebRTC / RTP |

#### 一句话取舍

> **点播、广电、存档、高码率音乐回放 → AAC;实时互动(RTC、语音通话、连麦)→ Opus。**

判据不是"谁音质高",而是"这条链路在不在乎那几十毫秒、在不在乎丢一个包"。在乎,就是 Opus 的主场。你接下来要打的 WebRTC 地基里,音频默认就是 Opus,把这一节的直觉带过去即可(见 [[project_webrtc_learning]])。

#### Opus 小节自检

- 同样传一段会议语音,为什么 Opus 默认 20ms 帧长比 AAC 的 1024 样本帧更适合通话?延迟差在哪一步?
- Opus 的 in-band FEC 和 PLC 有什么本质区别?哪个能"部分重建"真实内容、哪个只是"猜一段填上"?它们各自在什么时候被 Jitter Buffer 触发?
- 为什么 AAC 裸流必须套 ADTS,而 Opus 在 WebRTC 里可以不要外层封装?自描述信息分别从哪来?

---

## 十五、常见故障速查表

### 完全没声音

- 没找到音频流
- 解码器没成功打开
- `send_packet` / `receive_frame` 错误处理不完整
- PCM 没写入设备
- 设备没启动 / 音量 0 / 静音
- 时间戳错导致一直不被调度
- 声道布局或采样格式设备不支持

排查：

- 打印 codec / sample_rate / sample_fmt / channel_layout
- 确认 `frame->nb_samples > 0`
- 用固定正弦波测试播放链路
- dump PCM 出来用 ffplay 验证

### 刺耳杂音

- `FLTP` 被当成 `S16`
- Planar 被当成 Packed
- 位深错（S16 当成 S32）
- 字节序错
- buffer size 算错导致越界读
- 数据未初始化

**一直杂音 = 格式解释错；偶发爆音 = 缓冲区 / 溢出 / 线程**。

### 声音变快或变慢

- 采样率配置错
- 播放设备实际采样率和你以为的不一致
- 重采样输出参数错
- 时间戳换算错
- 样本数和字节数混淆

典型：

```
44100 PCM 按 48000 播 -> 变快、音调高
48000 PCM 按 44100 播 -> 变慢、音调低
```

### 声音断断续续

- 解码线程供数不足
- 音频回调里阻塞
- 缓冲区太小
- 网络抖动没 jitter buffer
- 线程优先级不够

排查指标：

- 环形缓冲区水位
- 每次回调请求字节 / 实际提供字节
- 解码耗时分布
- 系统 buffer size

### 音画不同步

- PTS time_base 换算错
- 音频时钟没扣设备缓冲延迟
- 视频按 DTS 而非 PTS 显示
- seek 后队列没清空
- 直播音视频队列堆积

排查：打印 audio PTS / video PTS / audio clock / system clock，观察差值是固定偏移还是持续漂移。

### 只有一边有声音

- Packed 读写步进错
- Planar 只读了 `data[0]`，没读 `data[1]`
- channel layout 错
- mono / stereo 转换没做

---

## 十六、工程化建议

### 集中管理音频参数

不要让 `sample_rate` / `channels` / `format` 散落各处。定义统一结构：

```cpp
struct AudioFormat {
    int sampleRate = 48000;
    AVSampleFormat sampleFmt = AV_SAMPLE_FMT_S16;
    AVChannelLayout chLayout {};
};
```

不然某一层改了某一个参数，另一层没同步，会出极其隐蔽的 bug。

### 初始化时打全日志

音频初始化时建议打印：

- codec name
- input / output sample rate
- input / output sample format
- channel layout
- decoder 输出格式
- resampler 输入输出
- device buffer size
- 每帧 `nb_samples`

这些日志在排查"杂音 / 变速 / 不同步"时是金子。

### 用 ffplay 验证 dump 的 PCM

裸 PCM 没有头信息，ffplay 必须手动指定参数：

```bash
ffplay -f s16le -ar 48000 -ac 2 dump.pcm        # S16 packed
ffplay -f f32le -ar 48000 -ac 2 dump.pcm        # FLT packed
```

参数写错听感也会错。这种 dump → ffplay 验证法能**快速分离"解码问题"和"播放问题"**。

---

## 十七、核心公式

```
duration_seconds = nb_samples / sample_rate

10ms 样本数 = sample_rate / 100
20ms 样本数 = sample_rate / 50

Packed PCM bytes = nb_samples × channels × bytes_per_sample

S16 -> float :  f = s16 / 32768.0
float -> S16 :  s16 = clamp(f, -1.0, 1.0) × 32767

pts -> seconds :  seconds = pts × av_q2d(time_base)
```

---

## 十八、自检

> 口述答案与「翻车回答」见 [09-题集与自检.md §4](./09-题集与自检.md#4-音频-pcm-与重采样对应-04-音频pcm-采样-重采样md)。

| 自检问题 | 题集编号 |
|---|---|
| 播放 PCM 必须知道哪 5 个参数？少任何一个会怎样？ | Q4.1 |
| Planar 和 Packed 区别？搞错会怎样？ | Q4.8 |
| 为什么 FFmpeg 解 AAC 默认 `FLTP`？直接送 SDL 为什么刺耳？ | Q4.2 |
| 音频 decode 为什么要 while `receive_frame`？ | Q4.3 |
| 视频场景为什么常用 48000 而不是 44100？ | Q4.9 |
| 混音爆音 / 为什么复杂混音先转 float？ | Q4.10 |
| `nb_samples` 是总样本数还是每声道？一帧多长？ | Q4.11 |
| 重采样输出 buffer 为什么不能简单按比例算？ | Q4.4 |
| 音视频同步为什么以音频为主时钟？ | Q4.5 |
| 音频回调里为什么不能阻塞？Underrun 是什么？ | Q4.12 |
| AAC 编码为什么需要 `AVAudioFifo`？ | Q4.6 |
| AEC 为什么需要远端播放参考信号？ | Q4.7 |
| 实时互动为什么用 Opus 而不是 AAC？ | Q4.14 |
| Jitter Buffer / PLC（弱网音频） | Q8.6、Q9.3；本节 §十四 |
| Seek 之后为什么容易音画不同步？要清什么？ | Q4.13 |
| 听到杂音 / 变速 / 断续 / 不同步怎么反查？ | Q4.15 + §十五 |
