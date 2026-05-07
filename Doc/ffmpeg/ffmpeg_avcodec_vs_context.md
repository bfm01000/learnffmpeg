# FFmpeg 核心概念：AVCodec 与 AVCodecContext 的关系及初始化流程

在 FFmpeg 的编解码开发中，`AVCodec` 和 `AVCodecContext` 是两个最核心的结构体。理解它们的关系以及为什么初始化需要分步进行，是掌握 FFmpeg API 的关键。

## 1. 核心概念对比

### AVCodec：算法本体与能力说明书
* **本质**：它是真正的“幕后干活的算法本体”。在 C 语言底层，它是一个充满了**函数指针**（如 `init`, `decode`, `encode`, `close`）的结构体。
* **无状态 (Stateless)**：它不保存任何特定视频流的进度、宽高、帧率等信息。全局只需要一个实例（例如全局只有一个 H.264 解码器实例）。
* **能力声明**：它定义了当前算法支持哪些特性（支持的像素格式 `pix_fmts`、采样率、硬件加速能力等）。
* **通俗比喻**：就像是**“做红烧肉的绝密菜谱”**或者面向对象编程中**“类的方法”**。

### AVCodecContext：参数配置与状态环境
* **本质**：它是解码器的“实例”或“工作环境”，用来装载**参数**和**动态状态**。
* **有状态 (Stateful)**：它保存了当前正在处理的视频流的宽 (`width`)、高 (`height`)、像素格式 (`pix_fmt`) 以及解码进度等。
* **通俗比喻**：就像是**“你家厨房正在炖着的那锅肉”**（记录了放了多少盐、炖了多久）或者面向对象编程中**“对象的属性”**。

---

## 2. 初始化流程：为什么分两步？

在代码中，我们通常会看到这样的流程：
```cpp
// 1. 分配上下文
AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);

// 2. 拷贝参数
avcodec_parameters_to_context(codec_ctx, codecpar);

// 3. 打开解码器
avcodec_open2(codec_ctx, codec, nullptr);
```

很多人会疑惑：既然第一步已经传入了 `codec`，为什么第三步还要再 `open` 一次？

FFmpeg 故意将“分配上下文”和“打开解码器”分开，是为了**留给开发者修改参数的机会**。我们可以用**“买电脑并配置”**的过程来理解：

1. **`avcodec_alloc_context3(codec)` —— 买了一台空电脑，并贴上说明书**
   * 它只是在内存中 `malloc` 了一块 `AVCodecContext` 大小的空间，把 `codec_ctx->codec` 指向了你传进来的 `codec`。
   * 此时它只是一个**空壳**，所有参数都是默认值，**还没有准备好运行**。

2. **`avcodec_parameters_to_context(...)` —— 装系统、配参数**
   * 将从文件中读取到的实际参数（宽高、SPS/PPS等）填入这个空壳中。

3. **`avcodec_open2(codec_ctx, codec, ...)` —— 按下开机键，启动底层引擎**
   * 这才是真正的**“初始化并启动”**。FFmpeg 内部会做大量繁重的工作：
     * **检查参数合法性**：看看这个 `codec` 支不支持你填入的宽高和格式。
     * **分配内部私有内存 (Private Data)**：根据参数，分配只有该解码器内部才知道的复杂内存结构（如参考帧缓冲区，存在 `codec_ctx->priv_data` 中）。
     * **准备函数指针**：正式将底层算法的 `decode` 等函数指针挂载好，准备随时被调用。

---

## 3. 进阶：如果 alloc 和 open2 传入不同的 Codec 会怎样？

既然 `alloc` 已经绑定了 `codec`，为什么 `open2` 还要再接收一个 `codec` 参数？如果两次传的值不一样会发生什么？

FFmpeg 的内部处理逻辑是：**以 `avcodec_open2` 传入的 Codec 为最高优先级进行覆盖，但前提是它们必须是“兼容”的。**

### 情况 A：两个 Codec 的 ID 相同（最常见的高级用法）
* **结果**：成功打开，并使用 `open2` 传入的 Codec。
* **场景**：`alloc` 时传入的是 FFmpeg 默认的 H.264 软件解码器（`AV_CODEC_ID_H264`），但在 `open2` 时，开发者想强制使用 NVIDIA 的 H.264 硬件解码器（如 `h264_cuvid`，ID 也是 `AV_CODEC_ID_H264`）。
* **意义**：这是 FFmpeg 官方推荐的**硬件加速切换方式**之一。`open2` 允许你在启动的最后一刻“篡位”，替换掉底层的干活引擎。

### 情况 B：两个 Codec 的 ID 完全不同（错误用法）
* **结果**：大概率打开失败（返回错误码），或者发生未定义行为。
* **场景**：`alloc` 时按 H.264（视频）分配了上下文，`open2` 时强行塞入一个 AAC（音频）解码器。
* **原因**：FFmpeg 内部校验会发现 `codec_ctx->codec_type`（视频）和新传入解码器的 `type`（音频）不匹配，或者发现参数完全是垃圾数据，直接返回 `AVERROR(EINVAL)`。

### 情况 C：`open2` 传入 `nullptr`（常规用法）
* **结果**：成功打开，使用 `alloc` 时绑定的 Codec。
* **场景**：`avcodec_open2(codec_ctx, nullptr, nullptr)`。
* **原因**：FFmpeg 发现你没传具体的解码器，就会去检查 `codec_ctx->codec`，如果里面已经存了值，就会继续使用它进行初始化。

### 附：`avcodec_open2` 内部源码逻辑简析
```c
int avcodec_open2(AVCodecContext *avctx, const AVCodec *codec, AVDictionary **options) {
    // 1. 如果你传了 codec，就覆盖上下文里原有的 codec
    if (codec) {
        avctx->codec = codec;
        avctx->codec_id = codec->id;
    } 
    // 2. 如果你没传，就用上下文里原有的
    else if (avctx->codec) {
        codec = avctx->codec;
    } 
    // 3. 如果两边都没有，报错
    else {
        return AVERROR(EINVAL);
    }

    // ... 接下来根据最终确定的 codec 分配 priv_data 并初始化 ...
}
```