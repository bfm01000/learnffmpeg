# SwsContext 讲义

## 补充：SwsContext 与硬件解码 (GPU) 数据

**核心结论：是的，`SwsContext` 只能处理 CPU 内存（Host Memory）中的数据。如果你的帧数据是通过硬件解码（如 NVDEC、QSV、VideoToolbox）得到的，且数据还驻留在 GPU 显存（Device Memory）中，你不能直接将它传给 `sws_scale`。**

### 为什么不能直接用？

`sws_scale` 是一个纯 CPU 实现的软件图像处理函数。它在执行时，需要通过 CPU 的指令（如 x86 的 AVX/SSE，或 ARM 的 NEON）去逐字节读取 `AVFrame->data` 指向的内存地址。
* **软件解码得到的 AVFrame**：`data` 指针指向的是普通的系统内存（RAM），CPU 可以飞快地读写。
* **硬件解码得到的 AVFrame**：`data` 指针通常指向的是一个**硬件表面的句柄**（Hardware Surface Handle），或者是一个映射到 GPU 显存的特殊地址。如果 CPU 强行去读写这个地址，要么会触发段错误（Segmentation Fault）直接崩溃，要么会导致极其缓慢的 PCIe 总线回读（Bus Readback），性能惨不忍睹。

### 如何处理硬件解码的帧？

如果你使用了硬件解码，想要进行像素格式转换或缩放，通常有以下两种方案：

#### 方案一：先将数据从 GPU 拷回 CPU（Hardware Download）
这是最简单但性能有损耗的方法。你需要先调用 FFmpeg 的硬件传输 API，把显存里的图像“下载”到系统内存中，然后再交给 `sws_scale` 处理。

```cpp
// 假设 hw_frame 是硬件解码出来的帧，格式通常是 AV_PIX_FMT_NV12 或特定的硬件格式（如 AV_PIX_FMT_CUDA）
AVFrame* sw_frame = av_frame_alloc();
// 将硬件帧的数据下载到软件帧（CPU内存）
int ret = av_hwframe_transfer_data(sw_frame, hw_frame, 0);
if (ret >= 0) {
    // 此时 sw_frame 里的数据已经在 CPU 内存中了，通常是 NV12 格式
    // 接下来你就可以安全地把 sw_frame 交给 sws_scale 去转成 RGB 或做缩放了
    sws_scale(sws_ctx, sw_frame->data, sw_frame->linesize, ...);
}
```

#### 方案二：全程在 GPU 上处理（推荐，性能最高）
既然数据已经在 GPU 上了，最合理的做法是让 GPU 顺便把格式转换和缩放也做了，完全不占用 CPU 资源。FFmpeg 提供了强大的硬件滤镜（Hardware Filters）来实现这一点。

例如，使用 `scale_cuda`（NVIDIA）或 `scale_qsv`（Intel）滤镜：
1. 解码器输出位于 GPU 的硬件帧。
2. 将硬件帧送入 FFmpeg 的 `AVFilterGraph`。
3. 在滤镜图中配置硬件缩放/转换滤镜（如将 NV12 转为 RGB）。
4. （可选）如果最终需要保存为文件，再在滤镜图的最后，或者手动使用 `av_hwframe_transfer_data` 将处理好的 RGB 数据拷回 CPU。

**总结**：`SwsContext` 是纯软件的“老黄牛”，它只认得 CPU 内存。面对 GPU 里的“高科技”数据，要么你把它搬回 CPU 给老黄牛处理，要么你直接在 GPU 里用“高科技”流水线（硬件滤镜）一次性搞定。

这是一篇面向初学者到进阶学习者的 `SwsContext` 讲义。  
你可以把它和你当前的 `project/2_video_2_image/main.cpp` 一起看，因为你现在做的“视频抽帧保存成 PPM/JPEG”正好就是 `SwsContext` 最典型的使用场景。

---

## 0. 先说结论

一句话理解：

- `SwsContext` 是 FFmpeg 里 `libswscale` 模块的“图像转换上下文”
- 它主要负责做两类事情：
  - 图像尺寸缩放
  - 像素格式转换

最常见的例子：

- `YUV420P -> RGB24`
- `NV12 -> YUV420P`
- `1920x1080 -> 1280x720`
- 同时做“缩放 + 像素格式转换”

你现在的项目里，它的作用就是：

- 把视频解码出来的 `AVFrame`
- 从视频常见的 YUV 格式
- 转成图片保存更容易处理的 RGB24，或者转成 JPEG 编码器支持的格式

---

## 1. 为什么需要 SwsContext

### 1.1 解码后的 AVFrame 不一定是你想要的格式

视频解码器输出的帧，常见像素格式有：

- `AV_PIX_FMT_YUV420P`
- `AV_PIX_FMT_NV12`
- `AV_PIX_FMT_YUVJ420P`
- `AV_PIX_FMT_YUV444P`

但很多后续模块不一定直接接受这些格式。

例如：

- 你要自己写 `PPM` 文件，通常想要 `RGB24`
- 你要送给某个图像处理算法，它可能只接受 `BGR24`
- 你要给编码器重新编码，它可能要求特定 `pix_fmt`

所以“解码出来能看见帧”并不等于“这帧能直接拿去用”。

### 1.2 视频帧宽高也不一定符合后续要求

例如：

- 原视频是 `1920x1080`
- 你想导出缩略图 `320x180`
- 或者你要送给一个只支持固定分辨率的模块

这时不仅要换像素格式，还要做缩放。

`SwsContext` 正是专门干这个的。

---

## 2. SwsContext 到底是什么

你可以把它理解成：

- 一份“图像转换任务的配置对象”
- 里面记录了源图像和目标图像的转换规则

典型配置内容包括：

- 源宽度
- 源高度
- 源像素格式
- 目标宽度
- 目标高度
- 目标像素格式
- 使用哪种缩放算法

创建完成后，再通过 `sws_scale()` 真正执行转换。

所以通常是两步：

1. `sws_getContext(...)` 创建转换上下文
2. `sws_scale(...)` 执行转换

---

## 3. 它属于哪个模块

`SwsContext` 属于 FFmpeg 的 `libswscale`。

你在代码里通常会包含：

```cpp
extern "C" {
#include <libswscale/swscale.h>
}
```

在 `CMakeLists.txt` 里通常需要链接：

```cmake
libswscale
```

---

## 4. 典型使用流程

一个最常见的流程是：

```text
输入 AVFrame(比如 YUV420P)
    ->
创建目标 AVFrame(比如 RGB24)
    ->
创建 SwsContext
    ->
sws_scale 执行转换
    ->
得到转换后的目标帧
```

伪代码如下：

```cpp
AVFrame* src = ...;      // 解码器输出
AVFrame* dst = av_frame_alloc();

dst->format = AV_PIX_FMT_RGB24;
dst->width  = src->width;
dst->height = src->height;

av_frame_get_buffer(dst, 32);
av_frame_make_writable(dst);

SwsContext* sws = sws_getContext(
    src->width, src->height, (AVPixelFormat)src->format,
    dst->width, dst->height, AV_PIX_FMT_RGB24,
    SWS_BICUBIC,
    nullptr, nullptr, nullptr
);

sws_scale(
    sws,
    src->data,
    src->linesize,
    0,
    src->height,
    dst->data,
    dst->linesize
);
```

---

## 5. `sws_getContext()` 逐参数理解

原型可以粗略理解为：

```cpp
SwsContext* sws_getContext(
    int srcW, int srcH, AVPixelFormat srcFormat,
    int dstW, int dstH, AVPixelFormat dstFormat,
    int flags,
    SwsFilter* srcFilter,
    SwsFilter* dstFilter,
    const double* param
);
```

### 5.1 前三个参数：源图像信息

- `srcW`
- `srcH`
- `srcFormat`

表示“输入图像是什么样的”。

例如：

```cpp
frame->width
frame->height
static_cast<AVPixelFormat>(frame->format)
```

### 5.2 中间三个参数：目标图像信息

- `dstW`
- `dstH`
- `dstFormat`

表示“你想得到什么样的图像”。

例如：

- 如果只是转格式，不改尺寸：
  - `dstW = srcW`
  - `dstH = srcH`
- 如果要做缩略图：
  - `dstW = 320`
  - `dstH = 180`

### 5.3 `flags`：缩放算法或转换策略

常见的有：

- `SWS_FAST_BILINEAR`：快，质量一般
- `SWS_BILINEAR`：常用，速度和质量较平衡
- `SWS_BICUBIC`：质量更好，速度略慢
- `SWS_POINT`：最近邻
- `SWS_LANCZOS`：质量高，但更慢

如果你当前重点是学习和功能正确，`SWS_BICUBIC` 或 `SWS_BILINEAR` 都够用。

### 5.4 `srcFilter` / `dstFilter` / `param`

大多数入门和业务场景都传：

```cpp
nullptr, nullptr, nullptr
```

因为你只是做普通像素格式转换或缩放，不需要自定义滤镜参数。

---

## 6. `sws_scale()` 逐参数理解

原型可以粗略理解为：

```cpp
int sws_scale(
    SwsContext* c,
    const uint8_t* const srcSlice[],
    const int srcStride[],
    int srcSliceY,
    int srcSliceH,
    uint8_t* const dst[],
    const int dstStride[]
);
```

### 6.1 `c`

就是前面创建好的 `SwsContext`。

它代表：

- 输入图像长什么样
- 输出图像要变成什么样
- 用什么算法做转换

### 6.2 `srcSlice`

源图像各个 plane 的起始地址，一般就是：

```cpp
frame->data
```

例如：

- `YUV420P`
  - `data[0]` 指向 Y 平面
  - `data[1]` 指向 U 平面
  - `data[2]` 指向 V 平面
- `RGB24`
  - 通常主要使用 `data[0]`

### 6.3 `srcStride`

源图像每个 plane 每行实际占用的字节数，一般就是：

```cpp
frame->linesize
```

注意：

- 它不一定等于有效像素宽度
- 也不一定等于 `width * bytes_per_pixel`
- 因为很多图像缓冲区为了对齐，会在每行后面留一些 padding

### 6.4 `srcSliceY`

表示“从源图像的第几行开始转换”。

最常见写法是：

```cpp
0
```

表示从顶部开始处理。

### 6.5 `srcSliceH`

表示“这次转换多少行”。

最常见写法是：

```cpp
frame->height
```

表示整帧全部转换。

### 6.6 `dst`

目标图像各个 plane 的起始地址，一般就是：

```cpp
rgb_frame->data
```

转换后的结果会写到这里。

### 6.7 `dstStride`

目标图像各个 plane 每行的字节跨度，一般就是：

```cpp
rgb_frame->linesize
```

`sws_scale()` 会按照这个 stride 写入结果，而不是简单按 `width * bytes_per_pixel` 直写。

---

## 7. 为什么要用 `data[]` 和 `linesize[]`

这点非常重要。

### 7.1 `data[]`

表示每个 plane 的起始地址。

例如：

- 平面格式 `YUV420P`：
  - `data[0]` -> Y
  - `data[1]` -> U
  - `data[2]` -> V
- 打包格式 `RGB24`：
  - 常用 `data[0]`

### 7.2 `linesize[]`

表示每个 plane 一行实际跨度。

你不能自己假设：

```text
一行字节数 = width * 每像素字节数
```

因为实际内存中可能有对齐填充。

### 7.3 YUV420P 中 Y、U、V 不一定连续

逻辑上它们是三个 plane：

- Y 平面
- U 平面
- V 平面

物理内存上：

- 可能连续
- 也可能不连续

所以不能自己硬算：

```text
U = Y + width * height
V = U + width/2 * height/2
```

通用写法必须以：

- `data[0] / data[1] / data[2]`
- `linesize[0] / linesize[1] / linesize[2]`

为准。

---

## 8. `AVFrame` 相关的三个常见步骤

在使用 `SwsContext` 时，通常你会看到这三步：

### 8.1 `av_frame_alloc()`

只分配 `AVFrame` 这个结构体“壳子”。

这时：

- `data[]` 还没有真正指向图像缓冲区
- 只是有了一个空的 frame 对象

### 8.2 `av_frame_get_buffer()`

根据：

- `format`
- `width`
- `height`

为这个 frame 分配真正的像素缓冲区。

### 8.3 `av_frame_make_writable()`

在真正写入像素数据前，确认当前缓冲区可写。

如果缓冲区是共享的，FFmpeg 会在必要时为你准备一份新的可写缓冲区。

为什么要做这一步？

因为后面的：

```cpp
sws_scale(...)
```

会把转换结果写到目标 frame 的 `data[]` 中。

---

## 9. 你当前项目里的两个真实场景

### 9.1 保存 PPM

PPM 最终需要的是 `RGB24` 原始像素，因此流程是：

```text
解码器输出 AVFrame(通常是 YUV)
    ->
SwsContext 转成 RGB24
    ->
写 PPM 文件头
    ->
逐行写 RGB 数据
```

### 9.2 保存 JPEG

JPEG 不是原始像素，而是压缩编码后的图片文件，因此流程是：

```text
解码器输出 AVFrame
    ->
SwsContext 转成 JPEG 编码器支持的像素格式
    ->
送入 MJPEG 编码器
    ->
拿到压缩后的 AVPacket
    ->
写 .jpg 文件
```

这也是为什么保存 JPEG 不只是“直接写 frame 数据”。

---

## 10. 常见误区

### 误区 1：`SwsContext` 只是用来缩放尺寸

不对。  
它不仅能改尺寸，还能改像素格式。

很多时候你用它只是为了：

- `YUV420P -> RGB24`

根本没有改分辨率。

### 误区 2：`frame->data[0]` 就是一整张图

不一定。  
对于平面格式，图像可能分布在多个 plane 中。

### 误区 3：`linesize == width * bytes_per_pixel`

不一定。  
很多时候有对齐填充。

### 误区 4：Y、U、V 一定连续

不一定。  
必须按 `data[]` 和 `linesize[]` 来访问。

### 误区 5：保存 JPEG 不需要重新编码

不对。  
`AVFrame` 是原始像素，不是 `.jpg` 文件。

---

## 11. 资源管理

### 11.1 `SwsContext` 要释放

创建后最终要：

```cpp
sws_freeContext(sws_ctx);
```

### 11.2 目标 `AVFrame` 也要释放

例如：

```cpp
av_frame_free(&rgb_frame);
```

因为它内部缓冲区也是你申请出来的。

---

## 12. 性能和复用

在真实项目里，一个常见优化点是：

- 不要每处理一帧都重新 `sws_getContext()`

因为 `SwsContext` 本身是可以复用的，只要以下条件不变：

- 源宽高不变
- 源像素格式不变
- 目标宽高不变
- 目标像素格式不变

也就是说：

- 如果你在循环里连续处理很多帧
- 且它们的格式一致

通常应该：

1. 初始化时创建一次 `SwsContext`
2. 循环里反复调用 `sws_scale()`
3. 结束时再统一 `sws_freeContext()`

这样性能会更好。

你当前学习代码为了逻辑清晰，把保存 PPM/JPEG 的流程封装进单独函数里，每次新建一个 `SwsContext` 是可以接受的；但在工程化代码里通常会考虑复用。

---

## 13. 面试/实战中如何描述 SwsContext

如果别人问你“`SwsContext` 是干什么的”，你可以这样答：

> `SwsContext` 是 FFmpeg `libswscale` 里的图像转换上下文，用于完成视频帧的像素格式转换和分辨率缩放。实际使用时通常先通过 `sws_getContext()` 配置源图像和目标图像的信息，再通过 `sws_scale()` 执行转换。比如把解码出的 `YUV420P` 帧转成 `RGB24` 用于保存图片，或者转成编码器支持的像素格式后再进行编码。

---

## 14. 和你当前代码的关系

你现在的 `main.cpp` 里已经有两类典型使用：

- `save_frame_as_ppm(...)`
  - 把解码帧转成 `RGB24`
  - 然后写成 `PPM`

- `save_frame_as_jpeg(...)`
  - 把解码帧转成 JPEG 编码器需要的像素格式
  - 再送给 MJPEG 编码器

所以你现在已经不是在“抽象地学 `SwsContext`”，而是在真实项目里使用它了。

---

## 15. 你现阶段最应该记住的 5 句话

1. `SwsContext` 用来做像素格式转换和尺寸缩放。
2. `sws_getContext()` 负责配置转换规则，`sws_scale()` 负责真正执行转换。
3. 访问图像数据时，不要假设内存连续，要以 `data[]` 和 `linesize[]` 为准。
4. 保存 `PPM` 通常先转 `RGB24`；保存 `JPEG` 还要再经过图片编码器。
5. 格式和尺寸不变时，`SwsContext` 通常应该复用，而不是每帧重建。

---

## 16. 一个学习路线建议

如果你准备继续往下学，建议按这个顺序：

1. 搞清楚 `AVFrame` 的 `data[]` / `linesize[]`
2. 分清 `planar` 和 `packed`
3. 掌握 `YUV420P`、`NV12`、`RGB24` 的内存布局
4. 熟悉 `sws_getContext()` 和 `sws_scale()`
5. 再去看更复杂的颜色空间、色域、全范围/有限范围问题

这样学习会更稳。
