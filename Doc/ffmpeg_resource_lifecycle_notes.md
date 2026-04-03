# FFmpeg 资源生命周期与常见清理函数笔记

这篇笔记整理了 FFmpeg 中最容易混淆的几个资源管理概念，重点回答下面这些问题：

- `uninit`、`free`、`close`、`closep` 到底有什么区别
- 为什么它们都像是在“释放资源”，却不用同一个名字
- 常见 FFmpeg 对象分别应该怎么清理
- `AVCodecContext`、`AVChannelLayout`、`sample_aspect_ratio` 等概念在转码代码里分别扮演什么角色

---

## 1. 先说结论

在 FFmpeg 里，这几个名字虽然都和“资源清理”有关，但语义层级不同：

- `uninit`：反初始化内部状态，对象本身通常还在
- `free`：释放对象本身，很多时候也顺带清理内部资源
- `close`：关闭一个已经打开的资源、句柄或流程
- `closep`：关闭资源，并把传入的指针置空

它们不是随便起不同名字，而是为了明确告诉调用者：你现在是在“清空内容”、“关闭句柄”，还是“销毁整个对象”。

---

## 2. `uninit`、`free`、`close`、`closep` 的区别

### 2.1 `uninit`

`uninit` 的含义是：**把对象内部初始化出来的资源清理掉，但对象本身未必消失**。

典型场景：

- 结构体是局部变量，位于栈上
- 结构体本身由别处管理
- 当前只需要释放它内部附带的资源

例如：

```cpp
AVChannelLayout layout;
av_channel_layout_default(&layout, 2);
av_channel_layout_uninit(&layout);
```

这里 `layout` 这个变量本身还在，只是它内部的布局资源被清理了。

可以把 `uninit` 理解成：**清空家具，但房子还在**。

### 2.2 `free`

`free` 的含义是：**销毁整个对象**。  
通常它会先清理对象内部资源，再释放对象本身占用的内存。

例如：

```cpp
av_frame_free(&frame);
av_packet_free(&pkt);
avcodec_free_context(&codec_ctx);
```

这类函数的共同特点是：

- 对象本身是堆上分配出来的
- 调用后对象不再可用
- 很多实现还会把指针置为 `nullptr`

可以把 `free` 理解成：**把整栋房子拆掉**。

### 2.3 `close`

`close` 的含义是：**关闭一个已经打开的资源、句柄、文件或工作流程**。

它强调的是“关闭使用状态”，而不一定是“释放整个对象”。

例如：

- 关闭输入输出 IO
- 关闭文件
- 关闭编解码流程

它更像：

- 文件已经打开，现在要关掉
- 网络连接已经建立，现在要断开
- 输出流已经写入，现在要结束写入

可以把 `close` 理解成：**把门关上，不再继续使用**。

### 2.4 `closep`

`closep` 一般可以理解成：**close + pointer**。

也就是：

- 关闭资源
- 再把对应指针置空

例如：

```cpp
avio_closep(&ofmt_ctx->pb);
```

这个函数的好处是：

- 资源关闭了
- 指针变空了
- 可以避免悬空指针和重复关闭

可以把 `closep` 理解成：**关门后把地址也从纸条上擦掉**。

---

## 3. 为什么不能都叫一个名字

因为它们虽然都在处理资源生命周期，但处理的是不同层次的动作：

- `uninit`：对象还在，只清内部状态
- `free`：对象整个销毁
- `close`：结束“打开状态”
- `closep`：关闭并顺手把指针清空

如果都叫 `free`，使用者就很难判断：

- 这是清空内部字段，还是销毁整个对象
- 这是关闭文件，还是释放上下文
- 调用后对象还能不能继续用
- 指针会不会被置空

FFmpeg 这种命名方式，本质上是在通过 API 名字传达资源所有权和生命周期语义。

---

## 4. 常见对象怎么清理

下面这部分是转码项目里最常用的一张速查表。

### 4.1 `AVFormatContext`

#### 输入上下文

```cpp
avformat_close_input(&ifmt_ctx);
```

用于输入格式上下文，通常会：

- 关闭输入
- 释放相关资源
- 把指针置空

#### 输出上下文

```cpp
avio_closep(&ofmt_ctx->pb);
avformat_free_context(ofmt_ctx);
```

常见写法是先关输出 IO，再释放上下文。

注意：

- `avformat_free_context(ofmt_ctx)` 主要释放上下文本身
- 它不等于“自动帮你关闭已经打开的输出文件”

### 4.2 `AVCodecContext`

```cpp
avcodec_free_context(&codec_ctx);
```

用于编码器或解码器上下文，含义是：

- 释放上下文本身
- 一并清理其内部状态和私有数据

### 4.3 `AVFrame`

```cpp
av_frame_unref(frame);
av_frame_free(&frame);
```

区别：

- `av_frame_unref(frame)`：清掉当前帧持有的数据，但保留 `frame` 对象，便于复用
- `av_frame_free(&frame)`：连 `frame` 对象本身一起销毁

### 4.4 `AVPacket`

```cpp
av_packet_unref(pkt);
av_packet_free(&pkt);
```

区别与 `AVFrame` 类似：

- `unref`：清数据，留壳子
- `free`：壳子也一起销毁

### 4.5 `SwrContext`

```cpp
swr_free(&swr_ctx);
```

### 4.6 `SwsContext`

```cpp
sws_freeContext(sws_ctx);
```

### 4.7 `AVAudioFifo`

```cpp
av_audio_fifo_free(fifo);
```

### 4.8 `AVFilterGraph`

```cpp
avfilter_graph_free(&graph);
```

### 4.9 `AVChannelLayout`

```cpp
av_channel_layout_uninit(&layout);
```

这类对象很适合拿来理解 `uninit` 的语义：

- 它经常是一个结构体变量
- 不是单独 `malloc` 出来的“独立对象”
- 所以常见操作是清理内部资源，而不是“free 掉整个对象”

---

## 5. 最容易混淆的一组：`unref` 和 `free`

很多初学者真正最容易混的，其实不是 `uninit` 和 `free`，而是 `unref` 和 `free`。

一句话记忆：

- `unref`：把“当前装的内容”倒掉，容器还留着
- `free`：容器也不要了

例如对 `AVPacket`：

```cpp
av_packet_unref(pkt);   // 还能继续复用 pkt
av_packet_free(&pkt);   // pkt 整个销毁
```

对 `AVFrame` 也是一样的逻辑。

---

## 6. `AVCodecContext` 是既能编码也能解码吗

答案是：**是的，`AVCodecContext` 本身是通用的编解码器上下文类型。**

但要更准确地说：

- 它既可以承载编码器状态，也可以承载解码器状态
- 一个具体实例通常只承担一种角色
- 它到底是“编码上下文”还是“解码上下文”，取决于你绑定并打开的是哪种 `AVCodec`

常见搭配：

- 解码：`avcodec_find_decoder()`、`avcodec_send_packet()`、`avcodec_receive_frame()`
- 编码：`avcodec_find_encoder()`、`avcodec_send_frame()`、`avcodec_receive_packet()`

所以可以把它理解成一个“通用容器”，容器里装的是编码器运行状态还是解码器运行状态，取决于初始化方式。

---

## 7. `sample_aspect_ratio` 是什么

`sample_aspect_ratio` 表示的是：**像素宽高比**，也叫 SAR。

它描述的是：

- 一个像素是不是正方形
- 像素宽和像素高之间是什么比例

例如：

- `1:1` 表示正方形像素
- 某些视频虽然分辨率是 `720x576`，但像素不是正方形，最后显示比例仍可能是 `16:9`

它和显示宽高比 `display_aspect_ratio` 的关系大致是：

```text
DAR = 图像宽 / 图像高 * SAR
```

如果在转码时把解码器里的 `sample_aspect_ratio` 复制到编码器中，目的是：

- 保留原视频的显示比例信息
- 避免转码后画面被拉伸或压扁

---

## 8. `av_inv_q()` 是什么

`AVRational` 是 FFmpeg 用来表示分数的结构体：

```cpp
typedef struct AVRational {
    int num;
    int den;
} AVRational;
```

`av_inv_q(q)` 做的事非常简单：**把一个分数取倒数**。

例如：

- `1/25 -> 25/1`
- `1001/30000 -> 30000/1001`

它常用于帧率和时间基之间的换算：

- `fps = 25/1`
- `time_base = 1/25`

---

## 9. `bit_rate` 和 `bit_rate_tolerance` 是什么

这两个字段都属于编码器的码率控制参数。

### 9.1 `bit_rate`

表示目标平均码率，单位是 `bit/s`。

例如：

```cpp
enc_ctx->bit_rate = 1 * 1000 * 1000; // 1 Mbps
```

它决定的是：

- 你希望编码后码流大致达到什么量级
- 文件体积和画质之间的平衡偏向

### 9.2 `bit_rate_tolerance`

表示允许围绕目标码率波动的容忍范围，单位也是 `bit/s`。

例如：

```cpp
enc_ctx->bit_rate_tolerance = 100 * 1000; // 100 kbps
```

它表达的是：

- 编码器在做码率控制时，允许偏离目标码率多远

### 9.3 是否必须填写

不一定。

常见经验：

- `bit_rate`：在 ABR、CBR 这类按码率控制的场景里比较重要
- `bit_rate_tolerance`：很多情况下可以先不手动填写，使用默认行为
- 如果使用 CRF、QP 这类质量优先模式，`bit_rate` 不一定是核心参数

另外要注意：

- 这些字段是 FFmpeg 的通用接口
- 最终是否生效、影响多大，取决于具体编码器实现

---

## 10. `AVChannelLayout` 是什么

`AVChannelLayout` 表示的是：**音频声道布局**。

它不只是“有几个声道”，还描述了每个声道的语义位置，例如：

- 单声道
- 立体声
- 5.1
- 7.1

以及每个声道分别是：

- 左声道
- 右声道
- 中置
- 低频
- 环绕声道

如果代码里根据输入音频推导出一个 `AVChannelLayout`，再复制给输出编码器，核心意义就是：

- 让输出音频保持合理的声道配置
- 沿用输入布局，或者在缺失信息时回退到默认布局

---

## 11. 为什么 `av_channel_layout_copy()` 后有时要 `uninit()`，有时又不用

这是学习 FFmpeg 资源管理时一个非常典型的例子。

### 11.1 有临时变量时

例如：

```cpp
AVChannelLayout out_ch_layout = infer_channel_layout(dec_ctx);
av_channel_layout_copy(&enc_ctx->ch_layout, &out_ch_layout);
av_channel_layout_uninit(&out_ch_layout);
```

这里要 `uninit`，因为：

- `out_ch_layout` 是一个临时变量
- 它内部可能持有需要清理的资源
- 数据已经拷贝进编码器上下文，临时变量就该销毁

### 11.2 直接从源对象拷贝到目标对象时

例如：

```cpp
av_channel_layout_copy(&out_audio_enc_ctx->ch_layout, &in_audio_dec_ctx->ch_layout);
```

这里通常不用立刻额外写一个 `uninit`，因为：

- 没有临时中间变量
- 源布局属于输入解码器上下文，不是这行代码新建的
- 目标布局属于输出编码器上下文，后续会跟着编码器一起管理

但这不代表目标对象永远不用释放，而是说：

- 不需要“紧跟着这行代码”再手动释放
- 最终仍然要在编码器上下文销毁时整体清理

---

## 12. 为什么不能直接写结构体赋值

例如很多人会想写成：

```cpp
enc_ctx->ch_layout = out_ch_layout;
```

语法上未必报错，但资源语义上不安全。

原因在于：

- `AVChannelLayout` 不是适合随手浅拷贝的普通纯值类型
- 它内部可能带有需要专门管理的状态
- 简单赋值可能造成共享同一份内部资源
- 后续一旦重复释放或提前释放，就可能出问题

因此更稳妥、也更符合 FFmpeg API 设计的方式是：

```cpp
av_channel_layout_copy(&enc_ctx->ch_layout, &out_ch_layout);
```

---

## 13. `AV_SAMPLE_FMT_FLTP` 是什么

`AV_SAMPLE_FMT_FLTP` 可以拆成两部分理解：

- `FLT`：每个采样点是 `float`
- `P`：`planar`，平面存储

所以它表示：**浮点型、平面布局的音频采样格式**。

### 13.1 packed 和 planar 的区别

以双声道音频为例。

#### packed

数据交错存放：

```text
L R L R L R ...
```

#### planar

每个声道单独一块：

```text
L L L L ...
R R R R ...
```

因此当格式是 `AV_SAMPLE_FMT_FLTP` 时：

- `frame->data[0]` 往往是一整块左声道数据
- `frame->data[1]` 往往是一整块右声道数据

这种格式在 AAC 编码、重采样和滤镜处理中很常见。

---

## 14. 一套最实用的记忆法

如果你不想每次都翻文档，可以记下面这套简化版口诀：

- `unref`：清内容，留对象
- `uninit`：清内部状态，结构体还在
- `close`：关掉已经打开的资源
- `closep`：关闭并把指针置空
- `free`：整个对象销毁

放到转码工程里，大多数情况下就是：

- 循环复用包和帧：`av_packet_unref()`、`av_frame_unref()`
- 程序退出统一销毁：`av_packet_free()`、`av_frame_free()`、`avcodec_free_context()`
- 输入上下文：`avformat_close_input()`
- 输出上下文：先 `avio_closep()`，再 `avformat_free_context()`

---

## 15. 学习建议

如果你正在写一个完整的转码器，建议把资源对象分成三类来记：

1. **循环里反复复用的对象**
   例如 `AVPacket`、`AVFrame`
   重点记 `unref`

2. **程序初始化时创建、结束时统一销毁的对象**
   例如 `AVCodecContext`、`AVFormatContext`、`SwrContext`、`SwsContext`
   重点记 `free` / `close`

3. **临时结构体或中间配置对象**
   例如 `AVChannelLayout`
   重点记 `uninit`

这样理解之后，你会发现 FFmpeg 的资源清理命名虽然多，但其实很有规律。
