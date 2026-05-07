# Android 图形渲染与 BMGMedia SDK 架构复习指南

本文档总结了 Android 图形系统（Surface、Texture、BufferQueue）的核心概念，以及 BMGMedia SDK 中 `CameraRenderSurface`、`CameraLiveRender` 和 `NativeGLFilterAdapter` 的架构设计与使用场景。

---

## 一、 Android 图形系统核心概念

### 1. `android.view.Surface` 的本质
*   **表面现象**：在 Java 层，它只是一个简单的对象（“壳”），用于传递给各种多媒体组件（MediaPlayer、Camera、MediaCodec）。
*   **底层实质**：它是 Android 图形架构（BufferQueue）中**生产者（Producer）**的一个代理。
*   **核心机制**：在生产者和消费者之间流转的是一块块底层图像缓冲（`GraphicBuffer`）。它们通常位于图形硬件可访问的共享内存中，可被 GPU、显示硬件、编解码器等模块访问，不应简单等同于“某个 OpenGL 纹理对象本身”。
*   **C++ 层的映射**：在 JNI 中，通过 `ANativeWindow_fromSurface` 将 Java 的 `Surface` 转换为 C++ 的 `ANativeWindow` 指针，底层 EGL 才能向其渲染（`eglCreateWindowSurface`）。

### 2. Surface 的四种常见“芯”（消费者）
一个 `Surface` 的行为和性能，完全取决于它的**消费者**是谁：

| 来源 | 消费者 | 用途 | 性能/特点 |
| :--- | :--- | :--- | :--- |
| `SurfaceView` / `TextureView` | **SurfaceFlinger** (系统合成器) | 屏幕显示 | 基础渲染上屏路径。 |
| `MediaCodec.createInputSurface()` | **硬件视频编码器** | 录制、直播推流 | **完美零拷贝**，专为视频压缩设计。 |
| `new Surface(new SurfaceTexture(texId))` | **OpenGL OES 纹理** | 二次渲染、美颜滤镜 | **完美零拷贝**，跨模块 GPU 显存共享（如声网方案）。 |
| `ImageReader.getSurface()` | **CPU 内存** (ByteBuffer) | 截帧、纯 CPU 算法 | **极慢**，引发严重的 GPU -> CPU 内存拷贝。 |

### 3. Texture (纹理) vs Surface
*   **Texture**：GPU 内部私有的数据对象，可以直接在 Shader 中被采样（`texture2D`）用于二次渲染（如美颜、贴图）。
*   **Surface**：跨硬件模块的共享内存（GraphicBuffer）的生产者入口。OpenGL 不能把 `Surface` **直接当普通纹理采样**；通常只能向它渲染/写入。若想在 GPU 中继续处理其内容，通常要通过 `SurfaceTexture` 将其关联为 `OES` 纹理后再采样。
*   **转换代价**：
    *   面向 **GPU 继续处理** 时，正规路径基本只有：`Surface` -> `SurfaceTexture` -> `GL_TEXTURE_EXTERNAL_OES`。
    *   `ImageReader` 不是另一条“GPU 方案”，而是把图像取到 **CPU 内存** (`ByteBuffer`) 的退路，代价很高。
    *   换句话说，这里常说的“两种方法”并不是两个同级的 GPU 路径，而是：
        *   要继续走 GPU：用 `SurfaceTexture` / `OES Texture`
        *   要拿字节到 CPU：用 `ImageReader`
    *   为什么不能一律都用 `OES`：
        *   `OES` 的定位是“让 `SurfaceTexture` 接到的外部图像能被 GLES 采样”，它适合**采样再渲染**，不是通用 CPU 数据访问接口。
        *   有些业务目标本来就不是继续 GPU 渲染，而是必须拿到 CPU 可读数据，例如：截图保存、纯 CPU 算法、OCR / CV / AI 推理前处理、调试抓帧、给只接受 `ByteBuffer` 的库喂数据。
        *   即使已经进入 GPU，`OES` 也只是外部纹理；很多滤镜链、离屏处理、跨 API 使用时，往往还会先把它再绘制到普通 `2D Texture` / `FBO` 中，再继续后处理。
    *   因此，**做图像处理（美颜）一定要尽量在 Texture 阶段做，做最终输出（推流/上屏）才用 Surface；一旦已经写入最终 Surface，再想“拿回来处理”，通常就只剩 OES 接回 GPU 或 CPU 拷贝这两类路径，其中 CPU 拷贝最慢。**

### 4. `Surface` / `SurfaceTexture` / `OES Texture` / `BufferQueue` 关系

#### 4.1 一句话先记住
*   `Surface`：给生产者“写帧”的入口。
*   `SurfaceTexture`：`BufferQueue` 的消费者，负责“接帧”，并把接到的帧关联成 `OES Texture` 给 GL 采样。
*   `OES Texture`：`SurfaceTexture` 接到帧后，在 OpenGL 中暴露出来、可被 Shader 采样的纹理对象。
*   `BufferQueue`：连接生产者和消费者的底层缓冲队列机制，不是单纯“一个流程图”，而是 Android 图形系统里真正管理 buffer 流转、同步和复用的核心组件。
*   `GraphicBuffer`：在 `BufferQueue` 中实际被申请、复用和流转的底层图像缓冲。

#### 4.2 它们之间的真实关系
1. 生产者（如相机、OpenGL、MediaCodec、其他 SDK）向 `Surface` 写入一帧图像。
2. 这帧图像实际落在 `BufferQueue` 管理的一块 `GraphicBuffer` 中。
3. `SurfaceTexture` 作为消费者，从 `BufferQueue` 中获取最新可用帧。
4. 调用 `SurfaceTexture.updateTexImage()` 后，这块 `GraphicBuffer` 会被关联到一个 `GL_TEXTURE_EXTERNAL_OES` 纹理上。
5. 后续 Shader、美颜算法、二次渲染读取的就不再是 `Surface`，而是这个 `OES` 纹理。

如果想把关系再说得更精确一点，可以记成：

*   `Surface`：站在 `BufferQueue` 的**生产者侧**
*   `SurfaceTexture`：站在 `BufferQueue` 的**消费者侧**
*   `GraphicBuffer`：在这套生产者-消费者机制里真正流转的**图像数据载体**

所以不要理解成“`Surface` 底层是 `GraphicBuffer`，`SurfaceTexture` 底层是 `BufferQueue`”；更准确地说是：

*   `Surface` 和 `SurfaceTexture` 都建立在 `BufferQueue` 这套机制上
*   前者负责“把帧送进去”，后者负责“把帧接出来给 GL”
*   真正被队列管理和复用的是其中的 `GraphicBuffer`

#### 4.2.1 `BufferQueue` 是系统维护的吗
*   是的，`BufferQueue` 一般是 **Android 系统图形栈维护的底层机制**，不是业务开发者自己手写维护的一个普通队列。
*   应用层开发者通常不会直接“new 一个 `BufferQueue` 然后自己管理入队出队”，而是通过更高层的接口间接使用它，例如：
    *   `Surface`
    *   `SurfaceTexture`
    *   `SurfaceView` / `TextureView`
    *   `MediaCodec` 的输入输出 `Surface`
    *   相机预览相关接口
*   从业务视角看，开发者通常是在决定：
    *   谁当生产者，往哪个 `Surface` 写
    *   谁当消费者，用什么方式接帧
    *   帧到手后是直接显示、交给编码器，还是接入 GL 做处理
*   而底层 buffer 的申请、复用、排队、同步、fence 协调、可见帧切换等细节，通常由系统框架、驱动和图形栈共同处理。
*   所以你在业务流程图里经常“看不见 `BufferQueue`”，不是因为它不存在，而是因为它通常被封装在 `Surface` / `SurfaceTexture` 背后；只有在解释 Android 图形链路原理时，才会把它显式拿出来讲。

#### 4.3 为什么说 `Surface` 不能“直接读”
*   这里的“不能直接读”，是指不能像普通 `2D Texture` 一样，直接把 `Surface` 丢给 Shader 做 `texture2D` 采样。
*   但 `Surface` 背后的帧并不是不能继续处理，而是要先经过 `SurfaceTexture -> OES Texture` 这条桥接路径。
*   因此，后续美颜算法“读”的其实是 `OES Texture`，不是 `Surface` 本身。

#### 4.4 `OES Texture` 是什么
*   `OES Texture` 指的是 `GL_TEXTURE_EXTERNAL_OES`，它是 Android / OpenGL ES 上专门用于外部图像流接入的一类特殊纹理。
*   它常用于接收来自 `SurfaceTexture`、Camera、解码器等外部生产者的帧。
*   它可以被 Shader 采样，但和普通 `GL_TEXTURE_2D` 有一些限制（例如采样方式、不能像常规 2D 纹理那样随意上传像素）。
*   业务上可以把它理解成：**“给外部图像流准备的、可被 GPU 继续处理的纹理入口”**。

#### 4.5 `GraphicBuffer` 到底是什么，它的数据“在 GPU 上吗”
*   更准确的说法是：`GraphicBuffer` 通常位于**图形硬件可访问的共享内存**中，而不应简单理解成“某块只属于 GPU 的私有纹理内存”。
*   它的特点是：GPU 可以访问，显示硬件可以访问，视频编解码器通常也可以访问；某些场景下 CPU 也可以映射访问，但代价通常更高。
*   因此，`GraphicBuffer` 更像是**底层真实承载图像数据的缓冲区**，而 `Texture` 更像是 OpenGL 对这份图像数据的一种“可采样视图”或“处理接口”。
*   常见误区是把 `GraphicBuffer` 和 OpenGL 纹理对象混为一谈。二者关系更接近于：前者存数据，后者提供给 Shader 使用的访问方式。

#### 4.6 为什么说 `Surface` 更像“写入口”，却又能被后续处理
*   这不是因为底层 `GraphicBuffer` “只能写”，而是因为**`Surface` 这个接口在业务语境里主要暴露的是生产者能力**。
*   也就是说，拿到一个 `Surface` 时，常见操作通常是：
    *   OpenGL 往里渲染；
    *   Camera 往里输出；
    *   编码器/解码器把它作为输入或输出目标。
*   如果想让 GPU 后续继续“读”这帧，并不是直接“读 `Surface` 对象”，而是通过 `SurfaceTexture` 把其背后的最新 `GraphicBuffer` 接出来，再关联为 `OES Texture` 后供 Shader 采样。
*   如果想让 CPU 读取，则通常要走 `ImageReader`、buffer lock 等路径，这类路径往往更慢，也更容易带来拷贝和同步开销。

#### 4.7 `BufferQueue` 是什么，它是不是“一个流程”
*   `BufferQueue` 不是单纯的一张流程图，而是 Android 图形系统中的一套**生产者-消费者缓冲队列机制**。
*   它负责：
    *   管理多块 `GraphicBuffer` 的申请、复用和回收；
    *   协调生产者“写哪一块 buffer”；
    *   协调消费者“取哪一块已完成的 buffer”；
    *   通过 fence / sync 保证不会出现“同一块 buffer 一边写一边读”的冲突。
*   所以可以把它理解成：**图像帧流转时的中转站 + 调度器 + 同步器**。

#### 4.8 `BufferQueue` 是谁实现的，需要自己实现吗
*   **不需要自己实现。**
*   `BufferQueue` 是 Android 系统图形栈提供的基础设施，由系统框架和 native 图形组件实现。
*   应用层 / SDK 层开发通常只是**间接使用**它，而不是直接手写它。常见入口包括：
    *   `Surface`
    *   `SurfaceTexture`
    *   `SurfaceView`
    *   `TextureView`
    *   `ImageReader`
    *   `MediaCodec`
    *   Camera 输出 `Surface`
*   只有在 Android framework、图形驱动、平台层开发中，才需要真正深入到 `BufferQueue` 的内部实现。

#### 4.9 一个更准确的总理解
可以把这些对象的关系记成：

*   `GraphicBuffer`：底层真实图像数据所在的缓冲区；
*   `Surface`：让生产者往这块缓冲区“写图”的接口；
*   `BufferQueue`：负责在生产者和消费者之间排队、同步、复用这些缓冲区；
*   `SurfaceTexture`：从 `BufferQueue` 接出最新帧的消费者适配器；
*   `OES Texture`：把这帧以 OpenGL 可采样纹理的形式暴露给 Shader。

对应链路可简化为：

`Producer -> Surface -> BufferQueue -> SurfaceTexture -> OES Texture -> Shader`

### 5. `updateTexImage()` 的性能定位

#### 5.1 它做的不是“大拷贝”
`SurfaceTexture.updateTexImage()` 的核心工作通常是：
1. 从 `BufferQueue` 中拿到生产者最新提交的一帧；
2. 等待这帧真正可用（例如等待 GPU/硬件模块写完）；
3. 将该帧对应的 `GraphicBuffer` 关联到当前的 `OES Texture`；
4. 更新时间戳、变换矩阵等元数据。

因此，它通常**不是**把整帧像素从 GPU 拷贝到 CPU，也通常**不是**一次完整的像素复制；它更像是一次**接帧 + 同步 + 纹理关联**操作。

#### 5.2 它是不是耗时操作
*   **是有成本的，但通常不是链路里最重的那一个。**
*   它的主要成本往往来自**同步等待**，而不是像素搬运。
*   如果上游生产者还没把这一帧写完，`updateTexImage()` 可能会阻塞在 fence / buffer acquire 上，看起来“很慢”。

#### 5.3 和 `glReadPixels`、渲染到 `Surface` 相比
通常可按下面的经验理解：

*   **`glReadPixels` 最贵**：因为它往往意味着 GPU -> CPU 回读，常常打断 GPU/CPU 的并行流水线。
*   **渲染到 `Surface` 次之**：这是一次真实的 GPU 渲染开销，取决于 Shader、缩放、混合、拼接、防抖等具体复杂度。
*   **`updateTexImage()` 通常最轻**：它更偏向“接管最新 buffer 并绑定成 OES 纹理”，但在上游没准备好时可能因为同步而抖动。

可粗略记为：

`glReadPixels` >> 渲染到 `Surface` >= `updateTexImage()`

注意：这只是经验排序，不是绝对公式；如果某次渲染非常简单，而上游又存在明显同步等待，那么 `updateTexImage()` 也可能表现出较高耗时。

#### 5.4 零拷贝不等于零耗时
*   **零拷贝** 的含义是：尽量避免大块像素在 GPU 和 CPU 之间来回搬运。
*   但它**不代表没有同步成本**、没有队列管理成本、没有 Shader 成本，也不代表完全不会阻塞。
*   所以视频实时链路优化时，不能只盯“有没有拷贝”，还要关注：
    *   BufferQueue 深度和积压；
    *   上下游帧率是否匹配；
    *   `updateTexImage()` 是否频繁等待；
    *   美颜 / 拼接 Shader 是否过重；
    *   编码器输入 Surface 是否形成反压（back pressure）。

### 6. `GraphicBuffer` / `EGLImage` / `Texture` / `FBO` 到底是什么关系

这一组概念最容易混，因为它们都和“一帧图像”有关，但职责完全不同。最重要的区分是：

*   **谁是真实数据载体**
*   **谁是 OpenGL 的访问视图**
*   **谁是渲染输出目标**

#### 6.1 先给结论
*   `GraphicBuffer`：底层真实图像数据所在的缓冲区。它不应简单理解成“OpenGL 私有显存”或“某个 GL 纹理对象”，而更接近一块可被图形系统多个模块复用的底层图像存储。
*   `EGLImage`：跨 EGL / GL 共享图像资源的桥接对象，可把底层图像缓冲导入给 GL 使用。
*   `Texture`：OpenGL 中可被 Shader 采样的图像对象或视图。
*   `FBO`：OpenGL 的离屏渲染目标管理对象，用来决定“往哪张纹理写”。

可以粗略理解成：

`GraphicBuffer` 是“货物本体”，`EGLImage` 是“对接接口”，`Texture` 是“GPU 读取视图”，`FBO` 是“GPU 写入目标”。

#### 6.2 `GraphicBuffer`：图像数据真正躺在哪里
*   当 Camera、解码器、OpenGL、编码器等模块传递一帧图像时，底层真正被复用和流转的，通常是 `GraphicBuffer`。
*   它是面向系统图形栈的底层图像缓冲区，不等同于 Java 对象，也不等同于某个 GL 纹理 ID。
*   它也不必机械理解为“GPU 私有内存”。更准确地说，它是一块供 GPU、显示控制器、编解码器、相机等图形/多媒体硬件模块共享访问的图像 buffer；不同设备上，它背后的物理存储位置和映射方式可能不同。
*   在 Android 的零拷贝链路里，大家真正共享的往往是这块底层 buffer，而不是各自再复制一份像素。
*   这里说的“共享内存”，更偏向“跨图形/多媒体模块共享同一帧图像数据”的意思，不等同于教科书里单纯讲的进程间共享内存。手机和电脑都存在“共享图像资源”机制，只是具体实现方式会因 SoC、集显、独显和驱动模型不同而不同。

#### 6.2.0 `AHardwareBuffer` 和 `GraphicBuffer` 是什么关系
*   `AHardwareBuffer` 可以理解成：**Android 在 NDK 层对外公开的“硬件可访问 buffer”接口**。
*   `GraphicBuffer` 可以理解成：**Android Framework / 系统图形栈内部常见的底层图像缓冲对象**。
*   两者很多时候并不是“两块不同的内存”，而是站在不同层次上，对同一类底层硬件图像资源的描述：
    *   `GraphicBuffer`：更偏系统内部实现视角
    *   `AHardwareBuffer`：更偏 NDK / 应用可用的公开接口视角
*   所以在系统图形链路里，你更容易看到 `GraphicBuffer` 这个词；而在 NDK、Vulkan、EGL、跨 API 共享资源的语境里，你更容易看到 `AHardwareBuffer`。
*   可以粗略记成：
    *   `GraphicBuffer`：系统内部常说的底层图像 buffer
    *   `AHardwareBuffer`：应用和 NDK 能稳定使用的官方硬件 buffer 接口
*   `AHardwareBuffer` 存在的意义，不是因为系统里又多了一套新的图像内存，而是因为 Android 需要一个**对外稳定、可跨版本使用、可跨图形 API 共享**的原生接口；否则应用层就会被迫依赖系统内部实现细节。

#### 6.2.0.1 `AHardwareBuffer` 和 `EGLImage` 又是什么关系
*   可以把两者的分工理解成：
    *   `AHardwareBuffer`：**对外公开的底层硬件 buffer 句柄/接口**
    *   `EGLImage`：**把这块底层 buffer 接进 EGL / GL 世界的桥接对象**
*   也就是说，`AHardwareBuffer` 解决的是“这块共享图像资源如何以 NDK 公开接口被拿到和传递”，而 `EGLImage` 解决的是“这块资源如何被 EGL / OpenGL 引用和使用”。
*   因此，`AHardwareBuffer` 本身不是 OpenGL 纹理，也不是 Shader 直接采样的对象；通常还需要经过 `EGLImage` 这类桥接层，才能进一步绑定成 GL 里的纹理资源。
*   可以粗略想成这样一条链路：
    *   底层共享图像数据
    *   -> `AHardwareBuffer`（NDK 可见的公开接口）
    *   -> `EGLImage`（EGL / GL 的桥）
    *   -> `Texture`（GL 里真正被 Shader 使用的视图）
*   所以如果说：
    *   `GraphicBuffer` 更偏系统内部对底层图像缓冲的描述
    *   `AHardwareBuffer` 更偏应用/NDK 可拿到的公开接口
    *   那么 `EGLImage` 就更偏“把这块底层资源接给 GL”的桥接层
*   一句话总结：
    *   `AHardwareBuffer` 回答的是“这块 buffer 怎么对外暴露”
    *   `EGLImage` 回答的是“这块 buffer 怎么进入 GL”

#### 6.2.1 为什么“统一内存”下 CPU 拷贝仍然慢
*   手机上的 8GB、12GB 内存，通常是整机共享的系统 RAM，CPU、GPU、相机、编解码器、显示系统都会从中申请和访问资源。
*   但“底层是同一套物理内存”并不等于“CPU 访问图像”和“GPU 访问图像”成本一样。真正影响性能的，不只是数据放在哪里，还包括：**谁来搬、按什么布局搬、搬之前要不要等待同步、搬完之后谁还能直接继续用**。
*   CPU 路径慢，常见不是慢在单纯的 `memcpy`，而是慢在整条链路：
    *   **同步等待**：CPU 读之前，往往要先等 GPU / 编解码器 / 显示链路把这帧真正写完。
    *   **cache 与可见性处理**：不同硬件模块之间切换访问方时，常常需要 cache flush / invalidate、fence 同步等额外开销。
    *   **布局或格式转换**：很多图像 buffer 对 GPU/显示硬件友好，但不一定适合 CPU 直接线性读取；这时往往还会发生 stride 处理、plane 拆分、像素格式整理，甚至额外转换。
    *   **CPU 真的要碰每个像素**：一旦要把数据读成 `ByteBuffer`、`RGBA` 或 `YUV` 平面，CPU 就得老老实实遍历和搬运大量字节。
*   反过来，GPU 路径很多时候并不是“CPU 帮它拷到 GPU”，而是：
    *   直接复用同一个 `GraphicBuffer`
    *   或由 GPU / 图像硬件走专门的 copy、blit、采样渲染路径
    *   或者根本不是传统意义上的“完整像素拷贝”，而只是换了一种可采样视图
*   所以 `SurfaceTexture -> OES Texture` 往往很快，不是因为“同样的拷贝 GPU 天生更快”，而是因为它通常还留在 GPU 友好的零拷贝/低拷贝路径里；而 `ImageReader` 之类的 CPU 取数路径，意味着这帧图像要真正变成 CPU 可读字节数据，代价自然大很多。

#### 6.2.2 既然在同一块内存里，为什么 GPU 有时还要“拷贝”
*   理想情况下，GPU 当然希望直接读这块底层图像 buffer；很多高性能链路也确实是在这么做。
*   但“GPU 能访问这块内存”不等于“GPU 能按当前业务需要，直接把它当成目标资源来用”。是否还需要一次 GPU 内部转换/拷贝，通常取决于下面这些条件：
    *   **格式是否适合直接采样**：相机、编解码器、显示硬件友好的格式，不一定就是当前 Shader 最好处理的格式。
    *   **布局是否适合当前用法**：同样一帧数据，可能带有 stride、plane、tiling 或厂商私有布局；GPU 未必能以你想要的方式直接读。
    *   **资源形态是否够通用**：外部图像常常先以 `GL_TEXTURE_EXTERNAL_OES` 的形式接进来，但后续多级滤镜、离屏链路、FBO 渲染、更通用的 Shader 处理，往往更希望落到普通 `GL_TEXTURE_2D` 上。
    *   **同步和生命周期是否好控制**：即使同一块底层 buffer 可读，如果它还被相机、解码器、显示链路占用，业务侧通常也更愿意先转到自己可控的纹理/FBO，再做后续处理。
*   所以很多时候，GPU 的“拷贝”本质上不是“因为够不到那块内存，只能搬一份”，而是：
    *   把“外部共享图像”转成“更适合后续处理的 GPU 资源”
    *   把“可采样但受限的资源形态”转成“更通用的 `2D Texture` / `FBO` 链路”
    *   把“受外部模块驱动的 buffer”转成“应用自己更容易管理的纹理”
*   典型链路可以理解成：
    *   相机/解码器把帧写进 `GraphicBuffer`
    *   `SurfaceTexture` 把它接成 `OES Texture`
    *   GPU 先直接采样这份外部图像
    *   再把结果渲染到自己的 `GL_TEXTURE_2D + FBO`
    *   后续美颜、滤镜、叠加、合成都在这套更通用的纹理链上继续做
*   因此，很多“GPU 拷贝”并不是无意义的重复搬运，而更像一次**资源形态转换**：为了让后续处理更通用、更稳定、限制更少。

#### 6.3 `EGLImage`：把底层图像资源“接进” OpenGL 的桥
*   OpenGL 不能天然理解任何外部图像缓冲；很多时候需要一个 EGL 层的桥接对象。
*   `EGLImage` 的作用可以理解为：**把底层共享图像资源包装成 EGL / GL 可以引用的对象**。
*   然后 OpenGL 才能进一步把它绑定到纹理，或者作为其他图形资源使用。
*   不是所有业务代码都会显式操作 `EGLImage`，很多时候系统框架、驱动或 `SurfaceTexture` 已经帮你做了这层桥接。

#### 6.4 `Texture`：给 Shader 读的，不是给系统传帧的
*   `Texture` 是 OpenGL 里的采样对象，Shader 真正 `sample` 的是它。
*   普通处理链里最常见的是 `GL_TEXTURE_2D`；接外部图像流时常见的是 `GL_TEXTURE_EXTERNAL_OES`。
*   它更像“GL 看到这份图像的方式”，而不是“图像跨模块流转的底层载体”。
*   所以经常会出现这种情况：**底层还是同一份 `GraphicBuffer`，但在 GL 里被表现为一个 `OES Texture` 或普通 `Texture`。**

#### 6.4.1 `GL_TEXTURE_EXTERNAL_OES` 和 `GL_TEXTURE_2D` 到底差在哪
*   可以先记一个工程化结论：
    *   `OES Texture`：擅长把“外部图像”**接进来**
    *   `GL_TEXTURE_2D`：擅长把图像在 GL 内部**继续加工**
*   `GL_TEXTURE_EXTERNAL_OES` 的典型特点：
    *   常用于 `SurfaceTexture`、相机预览、解码输出这类“外部生产者”给到 GL 的图像。
    *   优点是接入外部图像自然，通常能留在低拷贝/零拷贝路径里。
    *   它本质上更像“外部图像在 GL 里的采样入口”，适合先采一遍、先显示一遍、先做一次接入转换。
    *   但它的使用限制通常比 `GL_TEXTURE_2D` 更多，很多场景下不如普通 2D 纹理灵活。
*   `GL_TEXTURE_2D` 的典型特点：
    *   是 OpenGL 里最通用、最常见的纹理形态。
    *   更适合挂到 `FBO` 上做离屏渲染，更适合多级滤镜链反复读写，也更符合大多数 Shader 和后处理链的直觉。
    *   业务里如果要做美颜、特效、叠加、贴图、混合、缩放、旋转、反复多 pass 处理，通常最后都会希望图像落到 `GL_TEXTURE_2D` 上。
*   所以两者常见分工不是“二选一”，而是“前后接力”：
    *   先用 `OES Texture` 把外部帧接进来
    *   再渲染到 `GL_TEXTURE_2D`
    *   后续所有通用图像处理都在 `GL_TEXTURE_2D + FBO` 链路里完成
*   可以把它们粗略类比成：
    *   `OES Texture`：外部图像进入 GL 世界的“接驳口”
    *   `GL_TEXTURE_2D`：GL 内部真正灵活好用的“标准工作纹理”
*   因此，工程里常见的“先 `OES`，再转 `2D Texture`”并不是多此一举，而是在平衡两件事：
    *   先尽量低成本接入外部帧
    *   再把它转换成更适合后续复杂处理的标准纹理资源

#### 6.5 `FBO`：不是纹理，也不是 buffer，而是“写到哪里”的控制器
*   `FBO`（Framebuffer Object）本身不存图像像素。
*   它的作用是让 OpenGL 知道：当前这次渲染的输出目标是谁。
*   常见做法是把一张 `GL_TEXTURE_2D` 挂到 `FBO` 上，然后把渲染结果写进这张纹理。
*   所以 `FBO` 更像“离屏渲染工作台”，而纹理是这张工作台上真正承接结果的那块图像面板。

#### 6.6 为什么实际工程里总是“先 OES，再转 2D Texture，再挂 FBO”
因为它们分别解决的是不同问题：

1. `OES Texture`：解决“外部来的帧，怎么进 GL”
2. `GL_TEXTURE_2D`：解决“后续滤镜链怎么灵活处理”
3. `FBO`：解决“处理结果往哪里写”

一个常见流程是：

1. 相机或 `SurfaceTexture` 提供一帧外部图像；
2. 这帧先以 `GL_TEXTURE_EXTERNAL_OES` 的形式进入 OpenGL；
3. 渲染一遍，把它转换到一张普通 `GL_TEXTURE_2D` 上；
4. 后续多个滤镜 / 美颜 / 特效都在 `GL_TEXTURE_2D + FBO` 链路上完成；
5. 最后再渲染到屏幕 `Surface` 或编码器 `InputSurface`。

#### 6.7 把几个对象放回同一条链路里
如果站在“相机 -> 美颜 -> 编码”的视角，一个典型链路可以理解为：

1. 相机或上游模块把一帧写入 `Surface`
2. 底层数据落在 `GraphicBuffer`
3. 通过 `BufferQueue` 传给 `SurfaceTexture`
4. `SurfaceTexture.updateTexImage()` 让这帧关联成 `OES Texture`
5. OpenGL 采样这个 `OES Texture`
6. 通过 `FBO` 把结果渲染到一张普通 `GL_TEXTURE_2D`
7. 后续滤镜链继续在多个 `FBO + Texture` 之间流转
8. 最终结果再渲染到屏幕 `Surface` 或 `MediaCodec InputSurface`

可简化成：

`Surface -> GraphicBuffer -> BufferQueue -> SurfaceTexture -> OES Texture -> FBO -> 2D Texture -> FBO -> Output Surface`

#### 6.7.1 一张更直观的总流程图
可以把整条链路再按“接入 -> 处理 -> 输出”分成三段来看：

```text
外部生产者（相机 / 解码器 / 上游 SDK）
        |
        v
     Surface
        |
        v
  GraphicBuffer
        |
        v
   BufferQueue
        |
        v
 SurfaceTexture
        |
        v
 OES Texture
   (先把外部帧接进 GL)
        |
        v
   FBO + GL_TEXTURE_2D
   (转成更通用的工作纹理)
        |
        v
 多级 FBO + Texture 滤镜链
 (美颜 / 特效 / 贴图 / 叠加)
        |
        v
 最终输出 Surface
 (屏幕 / MediaCodec InputSurface)
```

如果只记一句话，可以记成：

*   `Surface / GraphicBuffer / BufferQueue`：负责**跨模块传帧**
*   `SurfaceTexture / OES Texture`：负责**把外部帧接进 GL**
*   `GL_TEXTURE_2D + FBO`：负责**在 GL 内部做灵活处理**
*   `Output Surface`：负责**把最终结果交给显示或编码器**

#### 6.8 最容易混淆的误区
*   **误区 1：`GraphicBuffer` 就是纹理。**
    *   不是。`GraphicBuffer` 是底层数据缓冲，纹理是 GL 中的采样视图。
*   **误区 2：`FBO` 就是一张离屏图片。**
    *   不是。`FBO` 是渲染目标容器，真正存结果的通常是挂在其上的纹理或 renderbuffer。
*   **误区 3：`SurfaceTexture` 创建了新的图像数据。**
    *   不一定。很多时候它只是把已有的外部帧接进 GL，并暴露成 `OES Texture`。
*   **误区 4：`EGLImage` 是业务层必须手写操作的对象。**
    *   不一定。很多场景里它在更底层已经被系统或框架封装掉了。

#### 6.9 一句话总记忆
*   `GraphicBuffer`：真实数据
*   `EGLImage`：共享桥梁
*   `Texture`：采样视图
*   `FBO`：写入目标

### 7. 从相机到预览 / 美颜 / 编码 / 上屏的完整链路

前面介绍的对象如果不放回真实业务，很容易记混。这一节用一条典型视频链路，把“谁生产、谁消费、谁在 GPU 内处理、谁在最终输出”串起来。

#### 7.1 最常见的一条链路

```text
Camera
  -> 输出到 Surface
  -> 底层落到 GraphicBuffer
  -> BufferQueue 传给 SurfaceTexture
  -> updateTexImage()
  -> OES Texture
  -> Shader / FBO / 2D Texture 做美颜和特效
  -> 渲染到预览 Surface（上屏）
  -> 或渲染到 MediaCodec InputSurface（编码）
```

可以把它拆成四段：

1. **采集阶段**：相机把帧写到 `Surface`
2. **接入 GL 阶段**：`SurfaceTexture` 把帧接成 `OES Texture`
3. **图像处理阶段**：OpenGL 在 `Texture + FBO` 链路中完成美颜、滤镜、拼接
4. **输出阶段**：结果写到屏幕 `Surface` 或编码器 `InputSurface`

#### 7.2 第 1 段：相机采集阶段
*   Camera 或 Camera2 不会直接把一帧“交给 OpenGL 纹理”。
*   更常见的做法是：相机拿到一个 `Surface`，然后把图像写入它。
*   这时图像真正落在底层的 `GraphicBuffer` 中，并通过 `BufferQueue` 排队。

这一阶段的关键词是：

*   **生产者**：Camera
*   **写入入口**：`Surface`
*   **底层载体**：`GraphicBuffer`
*   **排队机制**：`BufferQueue`

#### 7.3 第 2 段：把外部图像接入 OpenGL
*   如果后面要做 GPU 美颜、滤镜、贴图，就必须把这帧接进 OpenGL。
*   这时 `SurfaceTexture` 作为消费者，从 `BufferQueue` 取出最新可用帧。
*   调用 `updateTexImage()` 后，这一帧会被关联到一个 `GL_TEXTURE_EXTERNAL_OES` 上。

这里要特别注意：

*   不是“相机直接输出了一张普通 GL 纹理”
*   而是“相机先写到 `Surface/GraphicBuffer`，再通过 `SurfaceTexture` 接成 `OES Texture`”

这一阶段的关键词是：

*   **消费者**：`SurfaceTexture`
*   **桥接动作**：`updateTexImage()`
*   **GL 中的外部图像形态**：`OES Texture`

#### 7.4 第 3 段：为什么通常还要把 `OES Texture` 转成 `GL_TEXTURE_2D`
虽然 `OES Texture` 已经能被 Shader 采样，但工程里通常不会长期把整条滤镜链都建立在 OES 上，原因包括：

*   `OES Texture` 主要是为“外部图像接入”准备的；
*   它的使用限制比 `GL_TEXTURE_2D` 更多；
*   后续多个滤镜、特效、贴图、离屏中间结果，更适合在普通 `2D Texture + FBO` 链路中完成。

因此常见做法是：

1. 先采样 `OES Texture`
2. 用一次渲染把它输出到一张普通 `GL_TEXTURE_2D`
3. 之后所有中间处理都在普通纹理之间进行

可以简单理解成：

*   `OES Texture`：负责“把外部帧接进来”
*   `GL_TEXTURE_2D`：负责“让后续图像处理更灵活”

#### 7.5 第 4 段：`FBO` 在这条链路里到底干什么
当你要做磨皮、滤镜、畸变矫正、拼接时，不可能每一步都直接画到屏幕上。

这时就需要离屏渲染：

*   先准备一张普通纹理
*   把这张纹理挂到 `FBO`
*   然后把当前处理结果渲染进去

于是链路会变成：

```text
输入纹理 -> Shader处理 -> FBO -> 输出纹理
```

多级滤镜时通常就是一连串：

```text
Texture A -> FBO1 -> Texture B -> FBO2 -> Texture C -> ...
```

所以在美颜 / 特效链路中：

*   纹理是“处理中间结果”
*   `FBO` 是“决定写到哪张纹理上”

#### 7.6 第 5 段：最终为什么又回到 `Surface`
处理完以后，结果还得交给别的模块消费。

常见有两个去向：

*   **上屏预览**：渲染到 `SurfaceView` / `TextureView` 背后的 `Surface`
*   **视频编码**：渲染到 `MediaCodec.createInputSurface()` 返回的 `InputSurface`

这再次说明：

*   **Texture 适合处理**
*   **Surface 适合输出给别的系统模块**

也就是说，一条完整链路里往往是：

*   从 `Surface` 进
*   在 `Texture` 里处理
*   再写回 `Surface`

#### 7.7 一张更完整的“谁在干什么”图

```text
[Camera]
  生产帧
    ↓
[Surface]
  给生产者写入
    ↓
[GraphicBuffer]
  底层真实图像数据
    ↓
[BufferQueue]
  排队 / 同步 / 复用
    ↓
[SurfaceTexture]
  作为消费者接帧
    ↓ updateTexImage()
[OES Texture]
  外部图像进入 GL 的纹理形态
    ↓ shader采样
[FBO + GL_TEXTURE_2D]
  完成美颜 / 滤镜 / 拼接 / 特效
    ↓
[Output Surface]
  上屏 or 编码输入
```

#### 7.8 如果是“预览”和“编码”同时存在，会发生什么
在实际业务里，一帧图像常常不只有一个去向。

例如：

*   一路给屏幕预览
*   一路给直播编码
*   还有一路给截图或分析

这时常见做法不是每路都重新从 CPU 拷贝一份，而是：

*   在 GPU 内部复用已有纹理结果
*   最终分别渲染到不同目标
*   不同目标可能对应不同 `Surface`

例如：

```text
处理后的 Texture
  -> 渲染到预览 Surface
  -> 渲染到 MediaCodec InputSurface
```

所以一帧图像可以有多个“输出口”，但仍然尽量保持在 GPU / `GraphicBuffer` 路径内流转。

#### 7.9 哪些步骤最容易引入延迟
把整条链路串起来后，真正容易出问题的通常是这几个位置：

*   **相机到消费者节奏不匹配**：`BufferQueue` 堆积，导致帧延迟增加
*   **`updateTexImage()` 等待上游完成**：表现为接帧阶段卡顿
*   **Shader 过重**：美颜、拼接、特效太复杂，单帧 GPU 时间过长
*   **FBO 链过长**：中间 pass 太多，带来额外渲染成本
*   **输出到编码器过慢**：编码器或其输入 `Surface` 形成反压
*   **走了 `glReadPixels` / CPU 路径**：最容易破坏实时性

#### 7.10 一句话总结完整链路
可以把整个 Android 图形视频链路记成：

**外部模块通过 `Surface + GraphicBuffer + BufferQueue` 传帧，OpenGL 通过 `SurfaceTexture + OES Texture` 接帧，通过 `Texture + FBO` 做处理，最后再把结果输出到另一个 `Surface` 给显示或编码模块消费。**

---

## 二、 BMGMedia SDK 渲染架构解析

### 1. 主线流程 (Main Preview Pipeline)
*   **职责**：接收相机原始数据 -> 拼接防抖 -> 生成内部 OpenGL 纹理 -> 渲染到手机屏幕（响应用户滑动交互）。
*   **特点**：`RenderModel` 是动态变化的（FOV、视角随用户操作改变）。支持裁剪（Crop）和比例调整（Aspect Ratio）。

### 2. 旁路输出：`CameraRenderSurface` (核心出口)
*   **职责**：提供一个**额外的数据出口**。将主线处理好的画面（通常是固定的 ERP 全景展开图），零拷贝地画到外部传入的 `Surface` 上。
*   **特点**：
    *   **完全解耦**：如果不传入 Surface，该分支不激活，对主预览流程无任何性能影响。
    *   **独立渲染模型**：通常配置为 `PLANE_STITCH`（2:1 平面拼接），不受主屏幕用户滑动的影响。
    *   **默认无滤镜**：初始化时会默认清空滤镜（`clipRenderInfo.setFilterInfos(null)`），以保证推流出去的是干净的原画面。
*   **典型应用**：对接 `MediaCodec` 进行硬件编码推流；对接声网等第三方 SDK 的 `Surface` 进行零拷贝传递。

### 3. 旁路输出：`CameraLiveRender` (软编/高级硬编)
*   **职责**：将渲染结果输出为 `AVFrame`。
*   **工作模式**：
    *   **常规模式 (CPU 读取)**：使用 `glReadPixels` 下载到内存，交由 `CameraLiveWriter` (x264) 进行**软件编码**推流。
    *   **高级模式 (AHardwareBuffer)**：在 Android 8.0+ 上，分配 `AHardwareBuffer` 格式的硬件帧，支持更高效的 CPU 读取，或在 Android 11+ 上支持 `BLOCK_MODEL_ENCODE` 终极硬编（目前有兼容性风险）。

### 4. 滤镜拦截：`NativeGLFilterAdapter` (二次开发利器)
*   **职责**：允许外部开发者在主线渲染管线中**拦截 OpenGL 纹理**。
*   **工作流程**：
    1. 开发者继承 `NativeGLFilterAdapter` 并注册到 `BMGSessionRender`。
    2. 在 `onDrawToTexture(TextureInfo[] inputTextures)` 回调中拿到当前帧的 Texture ID（如拼接好的 ERP 纹理）。
    3. 开发者使用自己的 Shader 或第三方美颜 SDK（相芯、商汤等）处理该纹理。
    4. 返回处理后的新 Texture ID 给 SDK 继续后续渲染。
*   **优势**：数据始终在 GPU 内部流转，**零拷贝**，是实现自定义美颜、特效的最佳方案。

---

## 三、 经典业务场景分析

### 场景：第三方客户（如声网）实现“全景美颜 + 直播推流”

**客户诉求**：获取相机输出的 ERP 纹理，进行自定义的美颜滤镜处理，最后推流直播。

**完美架构设计（零拷贝）**：
1.  **SDK 拼接**：BMGMedia SDK 完成双镜头拼接和防抖，准备好 ERP 画面。
2.  **声网提供入口**：声网在内部创建一个 OES 纹理，并基于此创建一个 `SurfaceTexture` 和 `Surface`。
3.  **零拷贝交接**：声网将这个 `Surface` 传给 BMGMedia SDK 的 `CameraRenderSurface`。SDK 将 ERP 画面直接画入该 Surface（底层 GraphicBuffer）。
4.  **声网接管纹理**：声网调用 `SurfaceTexture.updateTexImage()`，从 `BufferQueue` 获取最新帧，并在 GPU 内部将该 `GraphicBuffer` 关联到自己的 OES 纹理上（**零拷贝，但可能包含同步等待**）。
5.  **声网美颜**：声网使用自己的美颜算法处理该 OES 纹理。
6.  **声网硬编推流**：声网将美颜后的最终纹理，渲染到他们自己维护的 `MediaCodec` InputSurface 上，完成硬件编码和推流。

**总结**：在这个架构中，`CameraRenderSurface` 扮演了完美的“零拷贝数据快递员”角色，使得两个庞大的 SDK（BMGMedia 和 声网）能够通过 Android 底层的 `GraphicBuffer + BufferQueue` 机制实现高效率协同。后续声网做美颜时，读的是 `SurfaceTexture` 暴露出的 `OES Texture`；送编码器时，写的是 `MediaCodec InputSurface`。整个链路尽量避免了 GPU -> CPU 的大拷贝。