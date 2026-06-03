# C++ 音视频开发：常见音频问题、核心概念与面试指南

音频开发看起来比视频简单：没有复杂的画面渲染、没有 YUV/RGB、没有 GPU 纹理。但在真实工程里，音频反而更容易暴露底层能力：只要采样率、声道、时间戳、缓冲区、重采样、线程调度里有一个环节处理不好，用户马上就能听到“杂音、爆音、卡顿、延迟、不同步”。

这篇文档按从浅入深的方式整理 C++ 音视频开发中最常见的音频概念和问题，重点面向：

- 想系统理解 PCM、采样率、位深、声道、帧、时间戳的人
- 使用 FFmpeg、SDL、OpenSL ES、AAudio、AudioTrack、CoreAudio 做音频播放/采集的人
- 准备音视频 C++ 岗位面试的人

---

## 1. 先建立一张音频地图

一个典型的音频播放链路如下：

```text
压缩音频数据
    |
    | 解封装 demux
    v
AAC / MP3 / Opus / G.711 packet
    |
    | 解码 decode
    v
PCM 原始采样数据
    |
    | 格式转换 / 重采样 / 声道转换
    v
播放设备期望的 PCM
    |
    | 音频设备回调 / 写入系统缓冲区
    v
声卡 / 扬声器
```

一个典型的音频采集链路如下：

```text
麦克风
    |
    | 系统采集接口
    v
PCM 原始采样数据
    |
    | 降噪 / 回声消除 / 增益 / 重采样
    v
编码器期望的 PCM
    |
    | 编码 encode
    v
AAC / Opus / G.711 packet
    |
    | 封装 / 网络发送
    v
MP4 / FLV / RTP / WebRTC
```

面试里经常问的“杂音、变速、音画不同步、延迟高、爆音”，本质上都可以回到这张链路图里定位。

---

## 2. PCM：音频世界的 YUV

### 2.1 什么是 PCM？

**PCM（Pulse Code Modulation，脉冲编码调制）**是最常见的原始音频数据格式。它不是压缩格式，而是把连续的模拟声音波形按固定时间间隔采样，并把每个采样点量化成数字。

可以把 PCM 理解为：

```text
声音波形
  -> 每秒取很多个点
  -> 每个点用一个数字表示振幅
  -> 这些数字连续排列在内存里
```

如果说视频解码后得到的是 YUV/RGB，那么音频解码后得到的就是 PCM。

### 2.2 PCM 为什么不能直接“听懂”？

PCM 数据本身只是一串数字。要正确播放，必须同时知道这些参数：

- 采样率：每秒有多少个采样点，例如 `44100`、`48000`
- 位深或采样格式：每个采样点怎么表示，例如 `S16`、`FLT`
- 声道数：单声道、双声道、5.1
- 声道布局：哪个声道是左、右、中置、低音
- 内存排列：Planar 还是 Packed
- 字节序：大端还是小端，移动端和 PC 通常主要遇到小端

同一段字节，如果按不同参数解释，会变成完全不同的声音。

### 2.3 面试回答

**问题：什么是 PCM？它和 AAC/MP3 有什么区别？**

可以这样答：

> PCM 是原始音频采样数据，是模拟声音经过采样和量化后的数字表示；AAC、MP3、Opus 是压缩编码后的音频码流。PCM 数据量大，但可以直接送给音频设备播放；AAC/MP3 数据量小，适合存储和传输，但播放前必须先解码成 PCM。音视频开发里，解码器输出的是 PCM，音频设备最终消费的通常也是 PCM。

---

## 3. 采样率：时间轴上的密度

### 3.1 什么是采样率？

采样率表示每秒钟采集多少个音频采样点，单位是 Hz。

常见采样率：

- `8000 Hz`：电话窄带语音
- `16000 Hz`：语音识别、VoIP 常见
- `44100 Hz`：CD 标准
- `48000 Hz`：视频、直播、移动端、专业音频常见
- `96000 Hz` / `192000 Hz`：专业录音或高解析音频

`48000 Hz` 的意思是：每个声道每秒有 48000 个采样点。

### 3.2 为什么视频场景常用 48000，而不是 44100？

`44100 Hz` 来自 CD 音频历史；`48000 Hz` 在视频、广播、直播、移动设备里更常见。很多音频设备的硬件原生采样率就是 48k。工程上如果采集、处理、播放、编码都统一为 48k，可以减少重采样次数，降低延迟和失真风险。

### 3.3 采样率不匹配会发生什么？

如果一段 `44100 Hz` 的 PCM 被当成 `48000 Hz` 播放：

- 播放速度会变快
- 音调会变高
- 时长会变短

如果一段 `48000 Hz` 的 PCM 被当成 `44100 Hz` 播放：

- 播放速度会变慢
- 音调会变低
- 时长会变长

这类问题不是“编码坏了”，而是时间轴解释错了。

### 3.4 面试回答

**问题：采样率不一致时，能不能直接播放？**

可以这样答：

> 不应该直接播放。采样率决定 PCM 的时间尺度，如果源数据是 44100Hz，而播放设备按 48000Hz 消费，就会导致声音变速、变调。正确做法是在解码输出和播放设备之间做重采样，比如使用 FFmpeg 的 `libswresample`，把输入采样率转换成设备或编码器要求的采样率。

---

## 4. 位深、采样格式与动态范围

### 4.1 位深是什么？

位深表示一个采样点用多少 bit 表示。

常见格式：

- `U8`：8 位无符号整数
- `S16`：16 位有符号整数，范围约为 `-32768 ~ 32767`
- `S32`：32 位有符号整数
- `FLT`：32 位浮点，通常范围约为 `-1.0 ~ 1.0`
- `DBL`：64 位浮点

位深越高，理论动态范围越大，能表示的振幅细节越多。

### 4.2 为什么音频通常是有符号的？

声音波形围绕 0 上下振动，正负值分别表示波形的两个方向。所以 16 位及以上的 PCM 通常是有符号整数或浮点数。

### 4.3 `S16` 和 `FLT` 的典型场景

`S16`：

- 硬件播放兼容性好
- 数据量适中
- SDL、Android AudioTrack、很多设备默认支持
- 工程播放链路中最常见

`FLT` / `FLTP`：

- 适合音频算法处理
- 混音、滤波、增益调整时更不容易溢出
- AAC 等编码器或解码器常见输出/输入格式

### 4.4 爆音与削波

当音频样本超过格式能表示的范围时，就会产生削波（clipping）。

例如 `S16` 最大值是 `32767`，如果混音后理论值变成 `50000`，最终只能被截断成 `32767`。波形顶部被“削平”，听起来就是刺耳的爆音或失真。

混音时常见错误：

```cpp
// 错误示意：两个 S16 直接相加，容易溢出
int16_t out = sample1 + sample2;
```

更稳妥的思路：

```cpp
int mixed = static_cast<int>(sample1) + static_cast<int>(sample2);
mixed = std::max(-32768, std::min(32767, mixed));
int16_t out = static_cast<int16_t>(mixed);
```

如果要做复杂混音，通常会转成 float，在 float 域处理，再转换回目标格式。

### 4.5 面试回答

**问题：为什么混音后会爆音？怎么解决？**

可以这样答：

> 多路音频直接相加会导致振幅超过目标格式可表示范围，比如 S16 超过 32767 后会削波，波形被截断，听起来就是爆音。解决方式包括：混音前降低每路音量；使用 float 格式进行混音；混音后做限幅、归一化或动态压缩；最后再转换为播放设备需要的 S16 等格式。

---

## 5. 声道数与声道布局

### 5.1 声道数不等于声道布局

声道数只告诉你有几个声道；声道布局告诉你每个声道代表什么位置。

例如同样是 2 个声道，通常是：

```text
FL + FR
Front Left + Front Right
```

但 6 个声道可能是 5.1：

```text
FL + FR + FC + LFE + BL + BR
```

如果只知道“6 声道”，不知道布局，播放器就不知道每个声道应该送到哪个扬声器。

### 5.2 FFmpeg 新旧声道布局 API

旧版本 FFmpeg 常用：

```cpp
uint64_t channel_layout = AV_CH_LAYOUT_STEREO;
```

新版本 FFmpeg 更推荐：

```cpp
AVChannelLayout ch_layout;
av_channel_layout_default(&ch_layout, 2);
av_channel_layout_uninit(&ch_layout);
```

在 C++ 项目里要注意 FFmpeg 版本差异。新 API 更明确，也更适合表达复杂声道布局。

### 5.3 常见声道问题

常见现象：

- 只有左声道有声音
- 左右声道反了
- 人声很小，背景声很大
- 5.1 转双声道后声音发闷
- 单声道被当成立体声播放，速度或数据读取错乱

常见原因：

- 声道数配置错
- 声道布局没设置
- Planar/Packed 理解错
- downmix 策略不正确
- 每帧数据大小计算时漏乘声道数

### 5.4 面试回答

**问题：声道数和声道布局有什么区别？**

可以这样答：

> 声道数只表示有几个通道，比如 2 或 6；声道布局表示这些通道的空间含义，比如 stereo 是左前和右前，5.1 包含中置和低音声道。音频播放、重采样和编码时不能只关心声道数，还要关心布局，否则可能出现左右声道反、环绕声 downmix 错、人声丢失等问题。

---

## 6. Planar 与 Packed：最常见的杂音来源

### 6.1 Packed 交错格式

双声道 Packed 数据排列：

```text
data[0]: L0 R0 L1 R1 L2 R2 L3 R3 ...
```

特点：

- 所有声道交错放在同一块内存
- 播放设备常喜欢这种格式
- `AV_SAMPLE_FMT_S16`、`AV_SAMPLE_FMT_FLT` 是 Packed

### 6.2 Planar 平面格式

双声道 Planar 数据排列：

```text
data[0]: L0 L1 L2 L3 ...
data[1]: R0 R1 R2 R3 ...
```

特点：

- 每个声道单独一块平面
- 编码器、算法处理常喜欢这种格式
- `AV_SAMPLE_FMT_S16P`、`AV_SAMPLE_FMT_FLTP` 是 Planar

### 6.3 为什么 FFmpeg 解 AAC 常见输出 `FLTP`？

AAC 这类现代音频编码在内部经常做频域变换、心理声学分析、单声道独立处理。浮点和平面格式对这些计算更友好。所以 FFmpeg 解码 AAC 后经常输出 `AV_SAMPLE_FMT_FLTP`。

但很多播放设备期望 `S16` packed。直接把 `FLTP` 塞给播放设备，几乎必然是杂音。

### 6.4 面试回答

**问题：FFmpeg 解码 AAC 后直接播放出现刺耳杂音，可能是什么原因？**

可以这样答：

> 很可能是采样格式和内存排列不匹配。AAC 解码后常见输出是 `FLTP`，也就是 float planar；而 SDL、AudioTrack 等播放设备常配置为 `S16` packed。如果把 float planar 当成 int16 interleaved 解释，数值类型和声道排列都错，必然产生杂音。解决方式是用 `SwrContext` 做采样格式、声道布局和采样率转换。

---

## 7. 音频帧、样本数与缓冲区大小

### 7.1 音频里的一帧是什么？

视频一帧通常是一张图像。音频一帧不是一个采样点，而是一组连续采样点。

在 FFmpeg 的 `AVFrame` 中：

- `frame->nb_samples`：每个声道有多少个采样点（采样点数量 = 采样率 * 时间）
- `frame->sample_rate`：采样率
- `frame->format`：采样格式
- `frame->ch_layout` 或 `channel_layout`：声道布局
- `frame->data` / `extended_data`：音频数据指针

如果 `nb_samples = 1024`、`sample_rate = 48000`，这一帧音频时长是：

```text
1024 / 48000 = 0.021333... 秒 = 21.33 ms
```

### 7.2 AAC 为什么经常是 1024 个 samples？

AAC-LC 常见一帧包含 1024 个采样点。48k 采样率下，一帧约 21.33ms；44.1k 下，一帧约 23.22ms。

这也是很多音频播放、同步、网络发送场景中常见的时间粒度。

### 7.3 如何计算 PCM 数据大小？

Packed 格式：

```text
bytes = nb_samples * channels * bytes_per_sample
```

例如 48k、双声道、S16、10ms：

```text
nb_samples = 48000 * 0.01 = 480
bytes = 480 * 2 * 2 = 1920 bytes
```

Planar 格式：

```text
每个声道平面大小 = nb_samples * bytes_per_sample
总大小 = 每个声道平面大小 * channels
```

工程上建议使用 FFmpeg 工具函数：

```cpp
int size = av_samples_get_buffer_size(
    nullptr,
    channels,
    nb_samples,
    sample_fmt,
    1
);
```

### 7.4 `linesize` 的意义

音频 `AVFrame` 里的 `linesize[0]` 表示一个音频平面占用的字节数。它可能因为内存对齐而大于理论数据大小。

不要随便假设：

```text
linesize[0] == nb_samples * bytes_per_sample
```

更稳妥的方式是根据 FFmpeg 提供的 API 计算或按 `linesize` 处理平面数据。

### 7.5 面试回答

**问题：音频 `AVFrame->nb_samples` 表示什么？**

可以这样答：

> 它表示每个声道包含的采样点数量，而不是总采样点数量。比如双声道 frame 的 `nb_samples` 是 1024，表示左声道 1024 个 sample，右声道也 1024 个 sample。计算 PCM 字节数时，如果是 packed S16，需要乘以声道数和每个 sample 的字节数。

---

## 8. 重采样：不只是改采样率

### 8.1 重采样到底做什么？

在 FFmpeg 里，`libswresample` 的能力不只是改变采样率，还可以同时处理：

- 采样率转换：`44100 -> 48000`
- 采样格式转换：`FLTP -> S16`
- 声道数转换：`mono -> stereo`
- 声道布局转换：`5.1 -> stereo`
- Planar/Packed 转换

所以很多工程里虽然都叫“重采样”，实际是在做一组音频格式归一化。

### 8.2 什么时候必须重采样？

常见场景：

- 解码器输出格式和播放器输入格式不一致
- 麦克风采集格式和编码器输入格式不一致
- 多路音频混音前采样率不一致
- 不同平台音频设备要求不同
- 推流协议或编码器要求固定采样率

### 8.3 `swr_get_delay` 为什么重要？

重采样器内部可能有缓存和滤波延迟。输出样本数不能简单写成：

```text
out_samples = in_samples * out_rate / in_rate
```

更稳妥的计算方式：

```cpp
int64_t delay = swr_get_delay(swr, in_sample_rate);
int out_samples = av_rescale_rnd(
    delay + in_nb_samples,
    out_sample_rate,
    in_sample_rate,
    AV_ROUND_UP
);
```

否则可能出现输出缓冲区不够、丢尾音、偶发爆音等问题。

### 8.4 重采样的典型 C++ 流程

```cpp
SwrContext* swr = nullptr;

// 新版本 FFmpeg 推荐 swr_alloc_set_opts2，旧项目里也常见 swr_alloc_set_opts。
int ret = swr_alloc_set_opts2(
    &swr,
    &out_ch_layout,
    out_sample_fmt,
    out_sample_rate,
    &in_ch_layout,
    in_sample_fmt,
    in_sample_rate,
    0,
    nullptr
);

if (ret < 0 || !swr) {
    // handle error
}

ret = swr_init(swr);
if (ret < 0) {
    // handle error
}

int64_t delay = swr_get_delay(swr, in_sample_rate);
int out_nb_samples = av_rescale_rnd(
    delay + in_nb_samples,
    out_sample_rate,
    in_sample_rate,
    AV_ROUND_UP
);

uint8_t** out_data = nullptr;
int out_linesize = 0;

av_samples_alloc_array_and_samples(
    &out_data,
    &out_linesize,
    out_channels,
    out_nb_samples,
    out_sample_fmt,
    0
);

int converted = swr_convert(
    swr,
    out_data,
    out_nb_samples,
    const_cast<const uint8_t**>(in_data),
    in_nb_samples
);

// converted 表示每个声道实际输出了多少个 sample
```

### 8.5 面试回答

**问题：如何计算 `swr_convert` 输出 buffer 大小？**

可以这样答：

> 不能只按输入样本数等比例计算，因为重采样器内部有延迟和缓存。通常先用 `swr_get_delay` 获取当前输入采样率下的延迟，再加上本次输入样本数，用 `av_rescale_rnd` 换算到输出采样率，得到输出样本数。然后用 `av_samples_get_buffer_size` 或 `av_samples_alloc_array_and_samples` 按输出声道数、采样格式和样本数分配缓冲区。

---

## 9. 时间戳：音频同步的核心

### 9.1 音频 PTS 表示什么？

音频 PTS 表示这一帧音频应该从什么时间开始播放。

音频帧的持续时间可以由样本数计算：

```text
duration = nb_samples / sample_rate
```

如果使用 FFmpeg 的 time_base，常见换算是：

```cpp
double seconds = pts * av_q2d(time_base);
```

音频比视频更适合作为主时钟，因为音频卡顿和跳变对用户非常敏感，而视频可以通过丢帧或延迟显示来追音频。

### 9.2 音频时钟怎么维护？

常见方式：

```text
audio_clock = 当前正在播放的音频帧 PTS + 已经被设备播放掉的样本时长
```

但注意：把 PCM 写入系统缓冲区，不等于声音已经被用户听到。系统缓冲区里可能还有几十毫秒甚至几百毫秒的数据。

所以精确音频时钟要考虑：

- 已提交给设备的样本数
- 设备实际消耗进度
- 音频缓冲区剩余时长
- 系统 API 能否查询播放延迟

### 9.3 音画同步常见策略

最常见：以音频为主时钟。

```text
如果视频 PTS < 音频时钟太多：视频落后，考虑丢帧
如果视频 PTS > 音频时钟太多：视频超前，延迟显示
如果差值很小：正常显示
```

一般不频繁拉伸音频来追视频，因为人耳对音频变速、卡顿、音调变化很敏感。

### 9.4 面试回答

**问题：为什么播放器通常以音频为主时钟？**

可以这样答：

> 因为音频一旦卡顿、断裂或变速，用户非常容易感知；而视频可以通过轻微丢帧或调整显示间隔来追赶音频，用户感知相对弱。因此播放器常以音频播放进度作为 master clock，视频根据自己的 PTS 和 audio clock 的差值决定立即显示、延迟显示还是丢帧。

---

## 10. 缓冲区、延迟与卡顿

### 10.1 音频为什么需要缓冲区？

音频设备通常以固定节奏消费 PCM。应用层线程、解码线程、网络线程不可能永远稳定地按这个节奏生产数据，所以中间需要缓冲区削峰填谷。

缓冲区太小：

- 延迟低
- 但容易 underrun
- CPU 调度稍微抖动就爆音或断音

缓冲区太大：

- 播放稳定
- 但延迟高
- 直播、通话体验差

音频开发永远在稳定性和延迟之间取平衡。

### 10.2 underrun 和 overrun

`underrun`：

```text
播放设备要数据，但应用没准备好
结果：卡顿、断音、爆音
```

`overrun`：

```text
采集或网络数据来得太快，应用来不及消费
结果：缓冲区堆积、延迟升高、最终丢数据
```

### 10.3 播放回调里不能做什么？

很多音频系统使用高优先级回调线程拉取 PCM。这个回调里应该尽量只做轻量操作。

不建议在音频回调里：

- 阻塞等待锁
- 做磁盘 IO
- 做网络 IO
- 做复杂解码
- 动态分配大量内存
- 打大量日志

推荐方式：

```text
解码线程提前生产 PCM -> 放入环形缓冲区 -> 音频回调只取数据并拷贝
```

### 10.4 面试回答

**问题：音频播放出现周期性卡顿或爆音，你会怎么排查？**

可以这样答：

> 我会先判断是数据格式问题还是实时性问题。如果是一直刺耳杂音，优先查采样格式、声道、采样率；如果是周期性断音，优先查缓冲区 underrun。具体会看音频回调是否阻塞、解码线程是否供数不稳定、环形缓冲区水位是否过低、是否在回调里做了锁等待或内存分配，以及系统 buffer size 是否设置得太小。

---

## 11. 常见音频故障速查表

### 11.1 完全没声音

可能原因：

- 没有找到音频流
- 解码器没有打开成功
- `avcodec_send_packet` / `avcodec_receive_frame` 错误处理不完整
- 解码后没有把 PCM 写入播放设备
- 播放设备没有启动
- 音量为 0 或被静音
- 时间戳错误导致音频一直不被调度
- 声道布局或采样格式不被设备支持

排查建议：

- 打印音频流 codec、sample_rate、sample_fmt、channel layout
- 确认解码后 `AVFrame->nb_samples > 0`
- 确认写入设备的字节数大于 0
- 用固定正弦波测试播放链路
- 把解码后的 PCM dump 出来，用 `ffplay` 验证

### 11.2 刺耳杂音

可能原因：

- `FLTP` 被当成 `S16` 播放
- Planar 被当成 Packed
- 采样位深解释错
- 字节序错误
- buffer size 计算错导致越界读
- 解码数据未初始化或复用了脏内存

核心判断：

```text
一直杂音：大概率格式解释错
偶发爆音：大概率缓冲区、溢出、线程或数据断裂
```

### 11.3 声音变快或变慢

可能原因：

- 采样率配置错
- 播放设备实际采样率和你以为的不一致
- 重采样输出参数错
- 时间戳换算错
- 把样本数和字节数混淆

典型例子：

```text
44100Hz PCM 按 48000Hz 播放 -> 变快、音调变高
48000Hz PCM 按 44100Hz 播放 -> 变慢、音调变低
```

### 11.4 声音断断续续

可能原因：

- 解码线程供数不足
- 音频 callback 里阻塞
- 缓冲区太小
- 网络抖动没有 jitter buffer
- 线程优先级不够
- 频繁 malloc/free 或日志导致实时线程抖动

排查指标：

- 环形缓冲区当前水位
- 每次 callback 请求多少字节
- 每次实际提供多少字节
- 解码耗时分布
- 系统音频 buffer 大小

### 11.5 音画不同步

可能原因：

- PTS time_base 换算错
- 音频播放时钟没有扣除设备缓冲延迟
- 视频按 DTS 显示而不是 PTS
- seek 后音视频队列没有清空
- 解码、渲染、播放线程之间时钟不统一
- 网络直播中音频或视频队列堆积

排查建议：

- 打印 audio PTS、video PTS、audio clock、system clock
- 观察差值是固定偏移还是持续漂移
- 固定偏移通常是起始时间或设备延迟问题
- 持续漂移通常是采样率、时钟源或播放速度问题

### 11.6 音量太小或忽大忽小

可能原因：

- 输入源本身响度不同
- downmix 时权重不合理
- 多路混音没有归一化
- AGC 自动增益控制配置不当
- S16 与 float 转换缩放错误

常见转换：

```cpp
float f = s16 / 32768.0f;
int16_t s = static_cast<int16_t>(std::clamp(f, -1.0f, 1.0f) * 32767.0f);
```

### 11.7 左右声道反了或只有一边有声音

可能原因：

- Packed 读写步进错误
- Planar 数据只取了 `data[0]`
- channel layout 错
- mono/stereo 转换没有处理
- 输出设备配置声道数和实际写入不一致

---

## 12. C++ 工程里的音频架构建议

### 12.1 播放器线程模型

一个常见 C++ 播放器结构：

```text
Demux Thread
    -> audio packet queue
    -> Audio Decode Thread
    -> PCM / resample queue
    -> Audio Device Callback

Demux Thread
    -> video packet queue
    -> Video Decode Thread
    -> video frame queue
    -> Render Thread
```

音频线程重点：

- packet queue 负责压缩包缓存
- decode thread 负责解码和重采样
- PCM queue 或 ring buffer 负责给设备稳定供数
- audio callback 尽量只读 buffer，不做复杂逻辑

### 12.2 RAII 管理 FFmpeg 资源

C++ 项目里建议用 RAII 包装 FFmpeg 指针，避免异常路径泄漏。

示意：

```cpp
struct AVFrameDeleter {
    void operator()(AVFrame* frame) const {
        av_frame_free(&frame);
    }
};

using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;
```

注意：FFmpeg 很多 free 函数要求传入二级指针，直接写 deleter 时要确认语义正确。

### 12.3 音频参数要集中管理

工程中建议定义一个清晰的音频格式结构：

```cpp
struct AudioFormat {
    int sample_rate = 48000;
    AVSampleFormat sample_fmt = AV_SAMPLE_FMT_S16;
    AVChannelLayout ch_layout {};
};
```

不要在播放器、重采样器、设备层到处散落 `sample_rate`、`channels`、`format`，否则非常容易出现某一层参数改了，另一层没改的隐蔽 bug。

### 12.4 日志应该打印什么？

音频初始化时建议打印：

- codec name
- input sample rate
- input sample format
- input channel layout
- decoder output sample format
- resampler output sample rate
- resampler output sample format
- output channel layout
- device buffer size
- 每帧 `nb_samples`

这类日志对排查音频问题非常关键。

---

## 13. 编码相关问题：AAC、Opus 与帧大小

### 13.1 编码器不是想喂多少就喂多少

很多音频编码器有固定或推荐的 `frame_size`。

例如 AAC-LC 常见 `frame_size = 1024`。编码时通常要攒够 1024 个 samples 再送给编码器。

如果采集端每次给 10ms：

```text
48000Hz * 10ms = 480 samples
```

而 AAC 需要 1024 samples，就要做 FIFO 缓存：

```text
采集 480 -> FIFO
采集 480 -> FIFO
采集 480 -> FIFO 中已有 1440
取 1024 编码，剩余 416
```

FFmpeg 中常用 `AVAudioFifo` 管理这种样本缓存。

### 13.2 编码器输入格式

编码器通常会声明自己支持的采样格式：

```cpp
codec->sample_fmts
```

不能假设 AAC 编码器一定吃 `S16`。很多情况下它更偏好 `FLTP`。

正确流程：

```text
采集 PCM
  -> 重采样到编码器要求的 sample_rate / sample_fmt / channel_layout
  -> 按 frame_size 放入编码器
  -> 得到压缩 packet
```

### 13.3 面试回答

**问题：音频编码时，采集一帧就能直接送编码器吗？**

可以这样答：

> 不一定。音频采集回调给出的样本数和编码器要求的 frame_size 可能不同，比如 48k 下 10ms 采集是 480 samples，而 AAC 编码器常见需要 1024 samples。工程上通常使用 FIFO 缓存样本，攒够编码器 frame_size 后再送入编码器；最后 flush 时还要处理不足一帧的尾部数据。

---

## 14. 采集场景：AEC、NS、AGC

音频采集不是简单拿到麦克风 PCM。实时通信里通常还要处理：

- AEC：Acoustic Echo Cancellation，回声消除
- NS：Noise Suppression，噪声抑制
- AGC：Automatic Gain Control，自动增益控制
- VAD：Voice Activity Detection，语音活动检测

### 14.1 回声是怎么来的？

通话时，扬声器播放远端声音，这个声音又被本地麦克风采到，再传回远端。远端就会听到自己的声音，这就是回声。

AEC 需要同时知道：

- 近端麦克风采集信号
- 远端播放参考信号
- 播放到采集之间的延迟

如果延迟估计不准，回声消除效果会明显变差。

### 14.2 为什么 WebRTC 音频处理很强？

WebRTC 的音频模块长期面向实时通话优化，内置了成熟的 AEC、NS、AGC、VAD、jitter buffer、NetEQ 等能力。很多实时音视频项目会复用 WebRTC 的音频处理模块，而不是自己从零实现。

### 14.3 面试回答

**问题：AEC 回声消除为什么需要播放端参考信号？**

可以这样答：

> 回声本质上是扬声器播放的远端声音经过空气传播和设备路径后又进入麦克风。AEC 要从麦克风信号里估计并减去这部分回声，所以必须知道远端播放了什么，也就是参考信号。同时还要估计播放到采集之间的延迟，否则参考信号和麦克风中的回声对不齐，消除效果会很差。

---

## 15. 网络音频：抖动、丢包与 Jitter Buffer

### 15.1 网络包不是匀速到达的

实时音频通常每 10ms 或 20ms 发送一个包。但网络到达时间可能是：

```text
20ms, 20ms, 60ms, 5ms, 15ms, 80ms ...
```

如果播放端直接按到包时间播放，就会严重卡顿。

### 15.2 Jitter Buffer 做什么？

Jitter Buffer 的作用是把网络到达的抖动变成相对平滑的播放节奏。

它会：

- 缓存一小段音频包
- 按 RTP timestamp 或音频时间戳排序
- 处理乱序
- 发现丢包
- 需要时做 PLC 丢包隐藏
- 在延迟和抗抖动之间动态平衡

### 15.3 PLC 是什么？

PLC（Packet Loss Concealment）是丢包隐藏。它不能真正恢复丢失的声音，只能根据前后音频估计一段比较自然的替代信号，避免直接静音或爆音。

语音场景里，短时间 PLC 效果通常不错；音乐场景里更容易被听出来。

### 15.4 面试回答

**问题：实时音频网络抖动怎么处理？**

可以这样答：

> 播放端通常引入 jitter buffer，把不均匀到达的网络包缓存并按时间戳重新排序，再以稳定节奏输出给解码器或播放设备。jitter buffer 太小会抗不住抖动，太大会增加通话延迟。遇到丢包时可以使用 PLC 做丢包隐藏，避免直接断音。

---

## 16. 面试高频题汇总

### Q1：PCM 播放必须知道哪些参数？

必须知道采样率、采样格式、声道数、声道布局、Planar/Packed 排列、字节序。否则同一段字节会被错误解释，导致杂音、变速、声道错乱或无声。

### Q2：为什么 FFmpeg 解码出来的音频不能总是直接播放？

因为解码器输出的是它自然或高效的格式，不一定等于播放设备支持的格式。AAC 解码常见输出 `FLTP`，而设备可能需要 `S16` packed、48k、stereo。中间通常需要 `SwrContext` 做格式转换和重采样。

### Q3：音频播放出现杂音，优先查什么？

优先查格式解释是否一致：

- sample rate 是否一致
- sample format 是否一致
- channel count/layout 是否一致
- planar/packed 是否一致
- buffer size 是否按正确格式计算

如果是持续杂音，多半是格式错；如果是偶发爆音或断音，多半是 buffer、线程调度或数据不足。

### Q4：音频播放变快变慢是什么原因？

大概率是采样率解释错，或者播放设备实际采样率和数据采样率不一致。比如 44100Hz PCM 按 48000Hz 播放会变快、音调变高。解决方式是正确配置设备或做重采样。

### Q5：`nb_samples`、`sample_rate`、`channels` 如何计算时长和大小？

时长：

```text
duration = nb_samples / sample_rate
```

Packed PCM 字节数：

```text
bytes = nb_samples * channels * bytes_per_sample
```

注意 `nb_samples` 是每个声道的样本数，不是所有声道加起来的总数。

### Q6：为什么音频通常作为音视频同步主时钟？

因为人耳对音频卡顿、断裂和变速更敏感，而视频可以通过丢帧或调整显示时间追音频。因此播放器通常维护 audio clock，让视频根据 PTS 和 audio clock 的差值调整。

### Q7：如何降低音频播放延迟？

可以从这些方向优化：

- 减小设备 buffer size
- 减小应用层 PCM 缓冲
- 降低解码和重采样耗时
- 避免音频回调阻塞
- 使用低延迟音频 API，例如 AAudio、Oboe、CoreAudio
- 直播场景控制 jitter buffer

但延迟越低越容易 underrun，所以要在稳定性和延迟之间平衡。

### Q8：为什么 seek 后容易音画不同步？

seek 后旧的 packet queue、frame queue、重采样器缓存、解码器内部缓存可能还残留旧数据。如果不 flush，就可能播放到 seek 前的音频或视频帧。正确做法是清空队列、flush decoder、必要时重置 resampler 和 audio clock。

### Q9：AAC 编码为什么要用 FIFO？

因为采集回调给出的样本数不一定等于 AAC 编码器要求的 `frame_size`。例如采集一次 480 samples，而 AAC 常见一帧 1024 samples，需要用 FIFO 攒够一帧再编码。

### Q10：怎么判断音频问题是解码问题还是播放问题？

可以分层验证：

- 把解码后的 PCM dump 出来，用 `ffplay` 按正确参数播放
- 用程序生成正弦波，直接送播放设备
- 如果 dump 的 PCM 正常，播放正弦波异常，问题在设备播放链路
- 如果播放正弦波正常，dump PCM 异常，问题在解码、重采样或格式转换链路

---

## 17. 实战排查方法：用 ffplay 验证 PCM

如果你 dump 了一段裸 PCM，比如：

- `S16`
- 双声道
- `48000 Hz`
- packed

可以这样播放：

```bash
ffplay -f s16le -ar 48000 -ac 2 out.pcm
```

如果是 float packed：

```bash
ffplay -f f32le -ar 48000 -ac 2 out.pcm
```

注意裸 PCM 没有头信息，`ffplay` 不知道采样率、声道数和格式，必须手动指定。参数写错，听到的结果也会错。

---

## 18. 建议记住的核心公式

音频帧时长：

```text
duration_seconds = nb_samples / sample_rate
```

10ms 对应样本数：

```text
samples = sample_rate / 100
```

20ms 对应样本数：

```text
samples = sample_rate / 50
```

Packed PCM 字节数：

```text
bytes = nb_samples * channels * bytes_per_sample
```

S16 转 float：

```text
float_sample = s16_sample / 32768.0
```

float 转 S16：

```text
s16_sample = clamp(float_sample, -1.0, 1.0) * 32767
```

时间戳转秒：

```text
seconds = pts * av_q2d(time_base)
```

---

## 19. 学习路线建议

如果要真正掌握 C++ 音频开发，可以按这个顺序练习：

1. 用 C++ 生成 440Hz 正弦波 PCM，并用 `ffplay` 播放。
2. 手动实现 `S16 stereo packed` 的音量调整。
3. 手动把 `S16 stereo packed` 拆成左右声道，再合并回去。
4. 用 FFmpeg 解码 AAC，dump PCM，用 `ffplay` 验证。
5. 使用 `SwrContext` 把 `FLTP` 转成 `S16` packed。
6. 用 SDL 或系统音频 API 播放解码后的 PCM。
7. 加入环形缓冲区，观察 buffer underrun。
8. 实现 audio clock，并让视频按 audio clock 同步。
9. 做 seek，处理 flush、清队列、重置时钟。
10. 做采集、重采样、AAC 编码，使用 `AVAudioFifo` 对齐 frame_size。

---

## 20. 最后总结

音频开发的核心不是背 API，而是建立一套稳定的判断模型：

```text
听到杂音 -> 先查格式解释
听到变速 -> 先查采样率和时间轴
听到断音 -> 先查缓冲区和实时线程
看到不同步 -> 先查 PTS、time_base 和 audio clock
编码失败 -> 先查编码器支持的 sample_fmt、sample_rate、channel_layout、frame_size
```

面试时如果能把问题从“现象”拆回“PCM 参数、内存布局、时间戳、缓冲区、设备约束”这几个维度，基本就能体现出真实工程经验。