# 03 - SwsContext：图像缩放与格式转换

> 对应导读第 3.6 节"加工问题"。
> 这一篇覆盖 `libswscale` 模块的核心 API（`sws_getContext` / `sws_scale`）、典型使用流程、性能要点。
> 硬件帧的处理放在 [07-硬件编解码.md](./07-硬件编解码.md)。

---

## 一、SwsContext 解决什么问题

视频解码器吐出来的 `AVFrame`，**格式和尺寸不一定能直接被下游用**。

常见错配：

- 解码器输出 `YUV420P`，但你要写 PPM 文件需要 `RGB24`。
- 解码器输出 `NV12`，但你要送的算法只认 `BGR24`。
- 原视频是 `1920 × 1080`，但你想导出 `320 × 180` 的缩略图。
- 解码器输出 `YUV420P`，但下游 JPEG 编码器只支持 `YUVJ420P`。

`SwsContext` 就是处理这一层"加工车间"——**像素格式转换 + 尺寸缩放**。

最常见的转换组合：

```
YUV420P -> RGB24
NV12    -> YUV420P
1920x1080 -> 1280x720 (单纯缩放)
YUV420P 1920x1080 -> RGB24 1280x720 (缩放+格式同时做)
```

---

## 二、典型使用流程

两步：先 `sws_getContext` 配置转换规则，再 `sws_scale` 执行转换。

```cpp
extern "C" {
    #include <libswscale/swscale.h>
}

AVFrame* src = ...;  // 解码器输出的源帧

// 准备目标帧
AVFrame* dst = av_frame_alloc();
dst->format = AV_PIX_FMT_RGB24;
dst->width  = src->width;
dst->height = src->height;
if (av_frame_get_buffer(dst, 32) < 0) { /* 分配失败,清理返回 */ }
// 注意:刚 get_buffer 出来的帧 refcount==1、本来就可写,
// 这里不需要 av_frame_make_writable(dst)——那是给"可能被共享的帧"用的(见 01 §5.7)

// 创建转换上下文
SwsContext* swsCtx = sws_getContext(
    src->width, src->height, static_cast<AVPixelFormat>(src->format),
    dst->width, dst->height, AV_PIX_FMT_RGB24,
    SWS_BICUBIC,
    nullptr, nullptr, nullptr
);
if (!swsCtx) { /* 创建失败,清理返回 */ }

// 执行转换
sws_scale(
    swsCtx,
    src->data, src->linesize,
    0, src->height,
    dst->data, dst->linesize
);

// 用完释放
sws_freeContext(swsCtx);
av_frame_free(&dst);
```

`av_frame_alloc` / `av_frame_get_buffer` / `av_frame_make_writable` 三件套的含义见 [01-数据结构与生命周期.md](./01-数据结构与生命周期.md) §5.7。**注意上面没有调 `make_writable`**：它只在帧可能被多方共享时才需要，刚分配的目标帧不需要——这是一个常见的多余调用。

---

## 三、sws_getContext 参数逐个看

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

| 参数组 | 说明 |
|---|---|
| `srcW / srcH / srcFormat` | 源图像信息。通常直接用 `frame->width / height / format` |
| `dstW / dstH / dstFormat` | 目标图像信息。如果只是换格式不改尺寸，让源和目标宽高相同即可 |
| `flags` | 缩放算法。常用 `SWS_BILINEAR`（平衡） / `SWS_BICUBIC`（质量更好） / `SWS_LANCZOS`（最高质量） / `SWS_POINT`（最近邻，速度最快） / `SWS_FAST_BILINEAR`（速度优先） |
| `srcFilter / dstFilter / param` | 高级滤镜参数。**入门和绝大多数业务场景全部传 nullptr** |

flags 怎么选：

- 学习 / Demo / 缩略图：`SWS_BILINEAR` 或 `SWS_BICUBIC`
- 高质量离线处理：`SWS_LANCZOS`
- 实时播放、性能敏感：`SWS_FAST_BILINEAR`
- 像素艺术 / 不允许插值：`SWS_POINT`

---

## 四、sws_scale 参数逐个看

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

| 参数 | 含义 | 通常传什么 |
|---|---|---|
| `c` | 前面创建的 `SwsContext` | |
| `srcSlice` | 源各平面起始地址数组 | `frame->data` |
| `srcStride` | 源各平面每行字节跨度 | `frame->linesize` |
| `srcSliceY` | 从源第几行开始转换 | `0` |
| `srcSliceH` | 转换多少行 | `frame->height`（整帧） |
| `dst` | 目标各平面起始地址 | `dstFrame->data` |
| `dstStride` | 目标各平面每行字节跨度 | `dstFrame->linesize` |

`srcSliceY` 和 `srcSliceH` 这两个参数是给"切片处理"留的接口——你可以把一帧切成多个 slice 分批转换。99% 场景就是 `0` 和 `height`。

---

## 五、为什么是 data[] 和 linesize[] 而不是单一指针

这是新手最容易困惑的设计。原因有两个：

### 5.1 多平面格式天然需要多个指针

Planar 格式（YUV420P）有三个独立平面，必须分别指：

```
frame->data[0] -> Y 平面
frame->data[1] -> U 平面
frame->data[2] -> V 平面
```

Semi-Planar（NV12）有两个：

```
frame->data[0] -> Y 平面
frame->data[1] -> UV 交错平面
```

Packed（RGB24）才只用 `data[0]`。

### 5.2 行末有 padding，必须按 stride 走

详见 [02-像素格式与内存布局.md](./02-像素格式与内存布局.md) 第四节。`linesize` 不等于 `width × 字节数` 是常态，硬算偏移会花屏。

### 5.3 Y / U / V 在内存里不一定连续

即使是 YUV420P 一帧，三个平面在物理内存上**可能连续也可能不连续**——取决于谁分配的、怎么分配的。

所以**绝对不能这样硬算**：

```cpp
// 错误：假设三个平面紧密相邻
uint8_t* yPlane = frame->data[0];
uint8_t* uPlane = yPlane + width * height;       // 危险!
uint8_t* vPlane = uPlane + width / 2 * height / 2;
```

通用写法永远以 `data[]` 和 `linesize[]` 为准。

---

## 六、典型使用场景

### 6.1 视频抽帧保存 PPM

PPM（特别是 P6 格式）结构极简：一个文本文件头 + 后面跟 RGB 原始字节。**很适合学习时验证 sws_scale 的正确性**。

```
解码器输出 AVFrame (YUV)
    ↓
sws_scale 转 RGB24
    ↓
写 PPM 文件头 (P6 + width + height + 255 + '\n')
    ↓
逐行写 RGB 数据 (按 linesize 偏移!)
```

### 6.2 视频抽帧保存 JPEG

JPEG 不是裸像素，是压缩后的图片文件。所以中间还要一个编码步骤：

```
解码器输出 AVFrame
    ↓
sws_scale 转编码器支持的像素格式 (通常是 YUVJ420P)
    ↓
avcodec_send_frame (MJPEG encoder)
    ↓
avcodec_receive_packet (得到 .jpg 字节)
    ↓
写文件
```

注意 MJPEG 编码器通常要求 `YUVJ420P`（J 表示 JPEG 的 Full Range）而不是 `YUV420P`，喂错会报 deprecated 警告或者得到色彩范围错误的图片。

### 6.3 给 OpenGL 上传纹理

OpenGL 默认期望 RGBA / RGB。但现代播放器通常**不这样做**——直接把 YUV 三个平面分别作为三张纹理上传，让 GPU shader 自己做 YUV→RGB，省掉 CPU 上 sws_scale 的开销。

所以 sws_scale 在性能敏感的渲染路径里通常会被绕过，主要服务于"取一帧存图"、"转码""、"软件后处理"这类非渲染场景。

---

## 七、性能要点：SwsContext 要复用

**最常见的优化点**：不要每处理一帧都重新 `sws_getContext`。

`SwsContext` 是可以复用的，只要以下条件不变：

- 源宽高
- 源像素格式
- 目标宽高
- 目标像素格式
- 缩放算法 flags

工程化写法：

```cpp
// 初始化时创建一次
SwsContext* swsCtx = sws_getContext(...);

while (still_decoding) {
    // 解码出一帧
    avcodec_receive_frame(codecCtx, srcFrame);

    // 复用同一个 swsCtx
    sws_scale(swsCtx, srcFrame->data, srcFrame->linesize, 0, srcFrame->height,
              dstFrame->data, dstFrame->linesize);

    // 处理 dstFrame
    av_frame_unref(srcFrame);
}

// 退出时统一释放
sws_freeContext(swsCtx);
```

如果输入流的分辨率中途变化（直播切片、动态码率），需要重建 `SwsContext`。这也是 `sws_getCachedContext` 这个工具函数的用武之地——它会比对参数是否变化，没变就复用，变了就重新分配。

---

## 七点五、色彩空间与 Range：YUV→RGB 发灰/偏色的根源

[02-像素格式与内存布局.md](./02-像素格式与内存布局.md) 第 5 节讲过 BT.601 vs BT.709、Full vs Limited Range 错配会导致**画面发灰或偏色**。在 swscale 这一层，根源是：

**`sws_scale` 做 YUV↔RGB 转换时，默认按 BT.601 + Limited Range 处理**。如果你的源其实是 BT.709（1080p/4K 几乎都是）或 Full Range，转出来的 RGB 就会色彩不准（典型是发灰、肤色偏移）。

正确做法：`sws_getContext` 之后用 `sws_setColorspaceDetails` 告诉它源/目标的色彩空间和 range：

```cpp
const int *invTable  = sws_getCoefficients(SWS_CS_ITU709);   // 源是 BT.709
const int *table     = sws_getCoefficients(SWS_CS_ITU709);   // 目标
int srcRange = 1;   // 1 = Full Range(JPEG), 0 = Limited Range(MPEG)
int dstRange = 1;
sws_setColorspaceDetails(swsCtx, invTable, srcRange, table, dstRange,
                         0, 1 << 16, 1 << 16);   // brightness/contrast/saturation 默认
```

源的真实色彩空间和 range 从哪来：解码帧的 `frame->colorspace`（`AVColorSpace`，如 `AVCOL_SPC_BT709`）和 `frame->color_range`（`AVColorRange`，`AVCOL_RANGE_JPEG`=Full / `AVCOL_RANGE_MPEG`=Limited）。**别写死 BT.601**——按帧的实际字段设，才是发灰/偏色 bug 的根治。

---

## 八、常见误区

| 误区 | 真相 |
|---|---|
| `SwsContext` 只用于缩放尺寸 | 它**主要**也是为了像素格式转换。"YUV420P → RGB24 但分辨率不变"是它最常见的使用方式 |
| `frame->data[0]` 就是一整张图 | 只对 Packed 格式成立。Planar 格式数据散在多个平面 |
| `linesize == width × 字节数` | 不一定，行末常有 padding |
| Y / U / V 在内存里一定连续 | 不一定，必须按 `data[]` 和 `linesize[]` 访问 |
| 保存 JPEG 不用重新编码 | `AVFrame` 是原始像素，必须经过 MJPEG 编码器才能得到 `.jpg` 文件 |
| 硬件解码出的帧能直接喂 sws_scale | 见 [07-硬件编解码.md](./07-硬件编解码.md)：`sws_scale` 是纯 CPU 实现，处理 GPU 帧要么崩要么极慢 |

---

## 九、最该记住的 5 句话

1. `SwsContext` 同时做两件事：像素格式转换 + 尺寸缩放。
2. `sws_getContext` 配置规则，`sws_scale` 执行转换。
3. 访问图像数据永远以 `data[]` 和 `linesize[]` 为准，不要硬算偏移。
4. 保存 PPM 先转 RGB24，保存 JPEG 还要再过一个 MJPEG 编码器。
5. 格式和尺寸不变时 `SwsContext` 必须复用，不要每帧重建。

---

## 十、自检

- `sws_getContext` 和 `sws_scale` 各自负责什么？为什么分两步？
- 转换 `YUV420P → RGB24` 时，源帧的 `data` 数组里几个指针有效？目标帧呢？
- 为什么不能把 `frame->data[0]` 当作一整张图来 memcpy？
- 多线程场景下，多个解码线程共用一个 `SwsContext` 安全吗？（提示：不安全）
- 输入流的分辨率中途变化怎么办？
- 保存 PPM 和保存 JPEG 在使用 `SwsContext` 上有什么不同？
