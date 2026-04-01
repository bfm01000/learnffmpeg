# Android 图形渲染与 BMGMedia SDK 架构复习指南

本文档总结了 Android 图形系统（Surface、Texture、BufferQueue）的核心概念，以及 BMGMedia SDK 中 `CameraRenderSurface`、`CameraLiveRender` 和 `NativeGLFilterAdapter` 的架构设计与使用场景。

---

## 一、 Android 图形系统核心概念

### 1. `android.view.Surface` 的本质
*   **表面现象**：在 Java 层，它只是一个简单的对象（“壳”），用于传递给各种多媒体组件（MediaPlayer、Camera、MediaCodec）。
*   **底层实质**：它是 Android 图形架构（BufferQueue）中**生产者（Producer）**的一个代理。
*   **核心机制**：在生产者和消费者之间流转的是一块块真实的物理显存（`GraphicBuffer`）。
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
*   **Surface**：跨硬件模块的共享内存（GraphicBuffer）。OpenGL 只能把它作为**渲染目标（FBO）**往里写数据，**不能直接读取**。
*   **转换代价**：
    *   `Surface` -> `Texture`：必须通过 `SurfaceTexture` (OES 纹理)，或者低效的 CPU 拷贝（`ImageReader`）。
    *   因此，**做图像处理（美颜）一定要在 Texture 阶段做，做最终输出（推流/上屏）才用 Surface。**

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
4.  **声网接管纹理**：声网调用 `SurfaceTexture.updateTexImage()`，在 GPU 内部将 GraphicBuffer 绑定到自己的 OES 纹理上（**零拷贝**）。
5.  **声网美颜**：声网使用自己的美颜算法处理该 OES 纹理。
6.  **声网硬编推流**：声网将美颜后的最终纹理，渲染到他们自己维护的 `MediaCodec` InputSurface 上，完成硬件编码和推流。

**总结**：在这个架构中，`CameraRenderSurface` 扮演了完美的“零拷贝数据快递员”角色，使得两个庞大的 SDK（BMGMedia 和 声网）能够通过 Android 底层的 GraphicBuffer 实现极高效率的协同工作。