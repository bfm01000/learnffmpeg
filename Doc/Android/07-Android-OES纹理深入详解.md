# Android OES 纹理深入详解（面试向）

> 适用方向：Android 音视频 SDK 开发、OpenGL ES 渲染、Camera/MediaCodec 管线
> 前置：OpenGL ES 基础（纹理、shader、sampler）、Android Surface/SurfaceTexture 概念
> 难度：⭐⭐⭐⭐⭐
> 关联：[[04-OpenGLES渲染与Surface详解]] · [[01-Camera2采集详解]] · [[02-MediaCodec硬编码实战]] · [[../iOS/11-IOSurface深入详解]]

---

## 一、面试问答

### Q1：OES 纹理是什么？一句话说清楚。

**面试官意图**：考察是否真正理解 OES 的本质，而不是背 API 名字。

**话术**：

> "OES 纹理，全称是 `GL_TEXTURE_EXTERNAL_OES`，是 OpenGL ES 的一个扩展——`GL_OES_EGL_image_external`。它的核心作用是：**让 OpenGL ES 能直接采样一块'由外部产生、非标准格式'的 GPU 内存，而不需要先把数据拷贝到 CPU 再上传成普通纹理。**"
>
> "这里'外部'指的是谁？Camera HAL 层、MediaCodec 解码器、或者任何通过 Android Surface 机制产出的 GraphicBuffer。这些 buffer 的格式通常是 YUV（NV12/NV21）、或者是厂商私有的 tiled/compressed 格式——GLES 根本不认识这些格式，没法当普通 `GL_TEXTURE_2D` 来用。OES 纹理就是 GLES 给这些'外部内存'开的一扇后门。"
>
> "核心流程四步：① Camera/MediaCodec 输出到 Surface → ② SurfaceTexture 收到新帧 → ③ `updateTexImage()` 内部把 GraphicBuffer 包成 EGLImage，通过 `glEGLImageTargetTexture2DOES` 绑定到 OES 纹理——不拷贝，只是 alias → ④ shader 里用 `samplerExternalOES` 采样，GPU 驱动在采样那一刻由硬件完成 YUV→RGB 转换。"
>
> "一句话：**OES 纹理是 Android 上实现 Camera 预览零拷贝的核心机制——它在 OpenGL ES 和 Android GraphicBuffer 之间架了一座桥，让 GPU 能直接采样 Camera 输出的 YUV 数据，全程 CPU 不参与。**"

---

### Q2：OES 纹理和普通 GL_TEXTURE_2D 有什么区别？为什么 OES 有那么多限制？

**面试官意图**：考察对 OES 限制的理解程度，以及是否知道限制背后的原因。

**话术**：

> "区别分三个层面：采样器类型、数据来源、以及由此带来的一整套使用限制。"
>
> "**采样器类型**：普通纹理在 shader 里用 `sampler2D` 采样；OES 纹理必须用 `samplerExternalOES`，而且 shader 开头必须声明 `#extension GL_OES_EGL_image_external : require`。这不是语法的区别——它告诉驱动'这块纹理的内容格式不是 GL 标准的 RGBA，采样时需要硬件做隐式转换'。"
>
> "**数据来源**：普通纹理由你自己通过 `glTexImage2D` / `glTexSubImage2D` 上传像素数据（malloc 出来的 buffer、解码出来的 RGBA 等）；OES 纹理的数据由外部产生（Camera HAL、MediaCodec、或其他 Surface 的生产者），你只能读、不能写。"
>
> "**使用限制**：
> - 不支持 mipmap——因为每帧数据都在变（Camera 30fps），生成 mipmap 链的开销太大，而且 OES 纹理的内容格式是 YUV/tiled，GPU 没法直接 downsample
> - 纹理 wrap 只支持 `GL_CLAMP_TO_EDGE`，不支持 `GL_REPEAT`——因为 OES 纹理通常是 Camera 画面，重复采样边缘没有意义，而且硬件 YUV→RGB 转换单元设计上没考虑 repeat 模式
> - 不能作为 FBO 的 color attachment——你不能往 OES 纹理上渲染。它只是'输入'，只能被采样。要做后处理，先把 OES 纹理采样渲染到一个普通 `GL_TEXTURE_2D` 上
> - 纹理坐标不是 [0,1] 就能正确映射——Camera 的画面可能被旋转、裁剪，必须配合 `SurfaceTexture.getTransformMatrix()` 在 vertex shader 里坐标变换"

**👨‍💻 追问：为什么 OES 纹理不能直接当 FBO 渲染目标，但普通 GL_TEXTURE_2D 可以？**

> "因为 OES 纹理底层的 GraphicBuffer 通常不是 GL 标准的 RGBA 格式——而是 NV12、NV21 或厂商私有的 YUV tiled 格式。FBO 渲染的时候，GPU 的 ROP（Render Output Unit）只能往标准的 RGB/RGBA 格式写——它不知道怎么写 YUV。OES 纹理只能被纹理单元（Texture Unit）'读取'（同时硬件做 YUV→RGB 转换），不能被 ROP '写入'。"
>
> "这就是为什么做美颜/Camera 滤镜的标准流程是：先把 OES 纹理采样渲染到一张 RGBA 的普通纹理上（这一步同时完成了 YUV→RGB 转换和坐标变换），然后再对这张 RGBA 纹理做后续的滤镜链处理。"

---

### Q3：`updateTexImage()` 内部到底做了什么？是拷贝还是 alias？

**面试官意图**：考察对 Android BufferQueue 和 EGLImage 机制的底层理解。

**话术**：

> "`updateTexImage()` 是零拷贝的——内部没有像素复制。它做的事情分四步："
>
> "**第一步**：从 SurfaceTexture 内部的 BufferQueue 里取出最新一帧的 GraphicBuffer。这个 GraphicBuffer 是 Camera HAL 或 MediaCodec 之前通过 Surface 的 `dequeueBuffer` / `queueBuffer` 投递进来的。"
>
> "**第二步**：用这个 GraphicBuffer 创建一个 `EGLImageKHR`。EGLImage 是 EGL 扩展——它本质上是对一块外部内存（这里就是 GraphicBuffer 的 gralloc 内存）的包装。创建 EGLImage 不分配新内存，只是记录了一个指针/引用。"
>
> "**第三步**：调 `glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, eglImage)`。这个调用把之前创建的 OES 纹理对象（glGenTextures 出来的那个 id）重新绑定到新的 EGLImage 上。此后这个纹理 id 指向的内存 = GraphicBuffer 的物理内存。"
>
> "**第四步**：释放上一帧持有的旧 GraphicBuffer（`releaseBuffer` 回 BufferQueue），让 Camera 可以继续往里写下一帧。"
>
> "**全程没有 `memcpy`、没有 `glTexImage2D`、没有 CPU 参与像素搬运**——这就是 Android 上 Camera 预览零拷贝的核心机制。"
>
> ```
> updateTexImage() 内部流程:
> 
> Camera HAL                        SurfaceTexture                     OpenGL ES
>    │                                  │                                  │
>    │ dequeueBuffer                    │                                  │
>    │ ← GraphicBuffer#1               │                                  │
>    │ 写入 NV12 帧                     │                                  │
>    │ queueBuffer(GraphicBuffer#1) ──► │ BufferQueue 队列里有了一帧        │
>    │                                  │                                  │
>    │                           updateTexImage():                         │
>    │                          ① acquireBuffer → GraphicBuffer#1           │
>    │                          ② eglCreateImageKHR(GB#1) → EGLImage       │
>    │                          ③ glEGLImageTargetTexture2DOES ──────────► │ OES tex → GB#1 内存
>    │                          ④ releaseBuffer(旧GB) 还给 Camera          │
>    │                                  │                                  │
>    │                           shader: samplerExternalOES                │
>    │                           采样 OES 纹理                             │
>    │                           → GPU 通过 EGLImage 直接读 GB#1 物理内存   │
>    │                           → 硬件 YUV→RGB，零拷贝 ✓                   │
> ```

---

### Q4：`getTransformMatrix()` 返回的矩阵是干什么的？为什么 OES 纹理需要它？

**面试官意图**：考察是否理解 Camera 方向和多分辨率裁剪在 OES 纹理中如何处理。

**话术**：

> "Android Camera 输出的画面可能是旋转的（后置摄像头 sensor 物理方向是横的，前置摄像头可能是镜像的），而且出图尺寸可能和 Surface 的尺寸不一致（系统做了裁剪/缩放）。SurfaceTexture 内部记录了这些变换参数，通过 `getTransformMatrix()` 返回一个 4×4 的浮点矩阵。"
>
> "**你必须在 vertex shader 里把这个矩阵乘到纹理坐标上**，否则画面方向不对——竖屏看到横着的画面，或者人脸是反的。"
>
> "这个矩阵封装了三件事：旋转（0°/90°/180°/270°）、镜像（前置摄像头的水平翻转）、以及裁剪区域的坐标映射（如果 Surface 尺寸和 Camera 出图尺寸不一致，系统可能只取 CropRect 区域）。"
>
> "```glsl
> // vertex shader 里的正确用法:
> uniform mat4 uTexMatrix;          // 从 getTransformMatrix() 拿到的矩阵
> attribute vec4 aTexCoord;         // 原始纹理坐标 [0,0,0,1] ~ [1,1,0,1]
> varying vec2 vTexCoord;
> 
> void main() {
>     vec4 tex = uTexMatrix * aTexCoord;  // ★ 矩阵乘法——旋转+镜像
>     vTexCoord = tex.xy / tex.w;          // 透视除法（通常 w=1，此步可省略）
>     gl_Position = ...;
> }
> ```
>
> "**注意**：这个矩阵的乘法必须在 CPU 侧完成（vertex shader 里做，每个顶点只算一次），不要在 fragment shader 里每个像素都乘——那是浪费。"

---

### Q5：OES 纹理在整个 Android 视频管线里扮演什么角色？把它放到全景图里看。

**面试官意图**：考察系统级视野——不是孤立地知道 OES，而是理解它在整个 Android 媒体栈中的位置。

**话术**：

> "OES 纹理是 Android 视频管线中**'GPU 可见的第一站'**。Camera 或者解码器产出的数据格式是 YUV——CPU 没法高效处理（每帧好几 MB，还要做色彩转换），但 GPU 可以直接消费。OES 纹理就是让 GPU 能'看到'这些 YUV 数据的窗口。"
>
> "整个管线的角色分工："
>
> ```
> ┌──────────────┐
> │  Camera HAL   │  产出 NV12/YV12 GraphicBuffer
> │  / MediaCodec │  (GPU 显存, tiled 布局)
> └──────┬───────┘
>        │ Surface (BufferQueue 的生产端)
>        ▼
> ┌──────────────┐
> │SurfaceTexture │  消费 GraphicBuffer，包装成 OES 纹理
> │+ OES Texture │  ← 全链路第一个"零拷贝点"
> └──────┬───────┘
>        │ samplerExternalOES (shader 采样, 硬件 YUV→RGB)
>        ▼
> ┌──────────────┐
> │  GL Renderer │  美颜/滤镜/特效 (全部在 GPU 上完成)
> │  (FBO→RGBA)  │  输出到普通 GL_TEXTURE_2D
> └──────┬───────┘
>        │
>   ┌────┴────────────┬──────────────┐
>   ▼                 ▼              ▼
> ┌────────┐   ┌──────────┐   ┌──────────┐
> │ 屏幕    │   │ MediaCodec│   │ 推流编码  │
> │(eglSwap │   │ 编码输入  │   │(FFmpeg/  │
> │ Buffers)│   │ (Surface) │   │ librtmp) │
> └────────┘   └──────────┘   └──────────┘
> ```
>
> "关键点：OES 纹理只管'输入'这一跳。后面的美颜、特效、编码输入，全是基于普通 GL_TEXTURE_2D 做的——先把 OES 纹理 render-to-texture 成一张 RGBA 纹理，然后再往下走。这条链路在上面的图里一目了然。"

**👨‍💻 追问：为什么不在 OES 纹理上直接做美颜？为什么要先 render-to-texture？**

> "两个原因。第一，OES 纹理不能作为 FBO 的渲染目标——你没法往上面画东西。美颜滤镜链需要多 pass 渲染，每个 pass 的输出需要作为下一个 pass 的输入，必须用普通纹理。第二，OES 纹理的格式是 YUV，而美颜 shader（磨皮、大眼、瘦脸）都是基于 RGB 颜色空间做的——你需要先拿到 RGB 值才能做肤色检测、高斯模糊这些操作。第一次 render-to-texture 同时完成了 YUV→RGB 转换和方向矫正，是必经之路。"

---

## 二、深入原理

### 2.1 `GL_OES_EGL_image_external` 扩展的规范视角

这个扩展由 Khronos 标准化，是 OpenGL ES 2.0/3.0 的可选扩展（但所有 Android 设备都支持）。它的核心定义：

```
扩展名: GL_OES_EGL_image_external
依赖:   GL_OES_EGL_image (EGLImage 创建/管理)

新增枚举:
  GL_TEXTURE_EXTERNAL_OES          = 0x8D65   // 新的纹理 target
  GL_SAMPLER_EXTERNAL_OES          = 0x8D66   // shader 中的采样器类型
  GL_REQUIRED_TEXTURE_IMAGE_UNITS_OES  ...    // 最小纹理单元数(通常 1)

新增 GLSL 关键字:
  samplerExternalOES   // 替代 sampler2D，用于采样外部纹理
  __samplerExternal2DOES  // ES 3.0 的版本

核心 API (EGL 侧):
  EGLImageKHR eglCreateImageKHR(EGLDisplay, EGLContext, target, buffer, attrs)
  //   target = EGL_NATIVE_BUFFER_ANDROID  → buffer 是 Android GraphicBuffer
  //   返回 EGLImage——不分配内存，只是对 buffer 的引用

  glEGLImageTargetTexture2DOES(GLenum target, GLeglImageOES image)
  //   把 OES 纹理对象绑定到 EGLImage 指向的内存
  //   此后该纹理的采样数据来自 EGLImage 的 backing store
```

**目标纹理 target 的限制（规范原文的精神）**：

规范明确 `GL_TEXTURE_EXTERNAL_OES` 的 `glTexParameteri` 只接受：
- `GL_TEXTURE_MIN_FILTER`：只支持 `GL_NEAREST` 或 `GL_LINEAR`（不支持 mipmap 相关的 filter）
- `GL_TEXTURE_MAG_FILTER`：同上
- `GL_TEXTURE_WRAP_S` / `GL_TEXTURE_WRAP_T`：只支持 `GL_CLAMP_TO_EDGE`

任何其他参数组合都是 `GL_INVALID_ENUM`。这就是为什么 OES 纹理有那么多"不能用"的限制——不是厂商偷懒，是规范就这么写的。

### 2.2 EGLImage 与零拷贝：从 GraphicBuffer 到 GL 纹理的映射链

这是理解 OES 纹理零拷贝的关键。整条链路上每一层都在同一个物理内存上叠一个新的"视图"：

```
GraphicBuffer (gralloc 分配)
  │ 物理内存: GPU 可见的 tiled NV12/YV12
  │ HAL 层: gralloc 模块管理, 由厂商实现
  │ 引用计数: GraphicBuffer 自己维护
  │
  ├─ Surface (BufferQueue 的生产端句柄)
  │    │ dequeueBuffer → 拿到一块空闲的 GraphicBuffer
  │    │ queueBuffer   → 投递到 BufferQueue 队列
  │
  ├─ SurfaceTexture (BufferQueue 的消费端句柄)
  │    │ acquireBuffer → 从队列取出一块 GraphicBuffer
  │    │ updateTexImage:
  │    │   1. 拿到 GraphicBuffer 的 ANativeWindowBuffer*
  │    │   2. eglCreateImageKHR(EGL_NATIVE_BUFFER_ANDROID, nativeBuffer)
  │    │      → 创建 EGLImage，不分配内存，只保存 nativeBuffer 引用
  │    │   3. glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, eglImage)
  │    │      → 把 OES 纹理对象的 backing store 指到 EGLImage 的内存
  │    │   4. eglDestroyImageKHR(eglImage)
  │    │      → 释放 EGLImage 包装（GraphicBuffer 不受影响）
  │    │
  │    └── shader 里 samplerExternalOES 采样
  │         → GPU 纹理单元通过 EGLImage→GraphicBuffer→物理内存
  │         → 硬件自动 YUV→RGB + detile
  │         → 零拷贝 ✓
  │
  └─ 对比 iOS 的等价链:
       CVPixelBuffer(IOSurface) → CVOpenGLESTextureCache → GL_TEXTURE_2D
       同样是零拷贝，但 iOS 不需要特殊的 texture target(GLES 用 GL_TEXTURE_2D)
       Android 多了一层 OES target，因为 GLES 不知道如何采样 YUV 格式的纹理
```

### 2.3 为什么 Android 需要 OES 而 iOS 不需要（GLES 视角）

这是一个深水区问题，大厂面试可能追问。

```
iOS (OpenGL ES, 已废弃但概念仍在):
  - CVPixelBuffer 底层是 IOSurface
  - CVOpenGLESTextureCache 创建的是 GL_TEXTURE_2D (不是 OES!)
  - 为什么能直接用 TEXTURE_2D? 
    → 因为 Apple 在驱动层做了处理: IOSurface 的 pixel format 信息
      已经告诉了驱动"这块内存虽然是 YUV，但你当它是 R8/RG8 采样就行"
    → 驱动在创建纹理时会把 IOSurface 的 plane 直接映射为 GL 可理解的单通道纹理
    → shader 里不需要 samplerExternalOES，用普通的 sampler2D

Android (OpenGL ES):
  - Camera/MediaCodec 输出的 GraphicBuffer 是完整的 YUV 帧 (NV12/NV21/...)
  - 没有 CVPixelBuffer 那样把 Y/UV 分成独立 plane 的上层抽象
  - GLES 驱动不认识 NV12 格式 —— 你不能用 glTexImage2D 上传 NV12 数据
  - EGLImage + OES 纹理是唯一让 GLES "看见" YUV 内存的方式
  - 代价: 需要 samplerExternalOES，shader 要多写 #extension 声明
```

### 2.4 OES 纹理的完整生命周期（代码视角）

```java
// ================================================================
// 阶段 1: 创建 OES 纹理 ID
// ================================================================
int[] textures = new int[1];
GLES20.glGenTextures(1, textures, 0);
int oesTextureId = textures[0];

// 绑定到 GL_TEXTURE_EXTERNAL_OES target
GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, oesTextureId);

// ★ 设置参数 —— 只能用这些，用别的会抛 GL_INVALID_ENUM
GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
                       GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR);
GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
                       GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR);
GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
                       GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE);
GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
                       GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE);

// ================================================================
// 阶段 2: 创建 SurfaceTexture，绑定到 Camera 输出
// ================================================================
SurfaceTexture surfaceTexture = new SurfaceTexture(oesTextureId);
//  ↑ 内部:
//    1. 创建了一条新的 BufferQueue
//    2. 把自己注册为这条队列的消费者
//    3. 记住了 oesTextureId —— 后续 updateTexImage() 就更新这个纹理

// 设置回调: 每当 Camera 往 Surface 里写入新帧，就通知我
surfaceTexture.setOnFrameAvailableListener(new SurfaceTexture.OnFrameAvailableListener() {
    @Override
    public void onFrameAvailable(SurfaceTexture surfaceTexture) {
        // ★ 收到通知后，在 GL 线程里调 updateTexImage
        //   注意: 回调线程是 SurfaceTexture 自己的线程，不是 GL 线程!
        //   应该设置一个 flag，让 GL 线程下一帧去调 updateTexImage
        frameAvailable = true;
    }
});

// 用 SurfaceTexture 创建一个 Surface，设置给 Camera2
Surface cameraSurface = new Surface(surfaceTexture);
// cameraDevice.createCaptureSession(..., cameraSurface, ...);

// ================================================================
// 阶段 3: 每帧更新 OES 纹理
// ================================================================
// 在 GL 渲染线程中:
if (frameAvailable) {
    surfaceTexture.updateTexImage();
    //  ↑ 内部: acquireBuffer → EGLImage → glEGLImageTargetTexture2DOES
    //    当前绑定的 oesTextureId 现在指向最新一帧 Camera 画面

    // 拿变换矩阵（旋转/镜像/裁剪）
    float[] texMatrix = new float[16];
    surfaceTexture.getTransformMatrix(texMatrix);
    //  ↑ 这个矩阵必须在 vertex shader 里乘到纹理坐标上

    frameAvailable = false;
}

// ================================================================
// 阶段 4: Shader 采样
// ================================================================
// Vertex Shader:
//   uniform mat4 uTexMatrix;
//   attribute vec4 aTexCoord;     // [0,0]→[1,1]
//   varying vec2 vTexCoord;
//   void main() {
//       vec4 tc = uTexMatrix * aTexCoord;
//       vTexCoord = tc.xy;       // 如果 w!=1 要做 tc.xy / tc.w
//       gl_Position = uMVPMatrix * aPosition;
//   }

// Fragment Shader:
//   #extension GL_OES_EGL_image_external : require
//   uniform samplerExternalOES sTexture;
//   varying vec2 vTexCoord;
//   void main() {
//       gl_FragColor = texture2D(sTexture, vTexCoord);
//       //  ↑ GPU 做硬件 YUV→RGB + detile
//   }

// ================================================================
// 阶段 5: 释放
// ================================================================
// surfaceTexture.release();  // 释放 BufferQueue 消费者
// GLES20.glDeleteTextures(1, new int[]{oesTextureId}, 0);
```

### 2.5 从 OES 纹理到编码器：第二条零拷贝路径

OES 纹理不仅可以渲染到屏幕，还可以作为 MediaCodec 编码器的输入——同样是零拷贝：

```java
// ================================================================
// 路径 A: OES → 屏幕 (渲染预览)
// ================================================================
// 1. OES 纹理 → FBO render to RGBA texture → eglSwapBuffers → 屏幕
//    (中间可以插入美颜/滤镜)

// ================================================================
// 路径 B: OES → MediaCodec 编码器 (零拷贝!)
// ================================================================
// 关键: 创建一个 EGLSurface，底层绑定到 MediaCodec 的 input Surface

// 1. 创建 MediaCodec 编码器, 拿到 input Surface
MediaCodec codec = MediaCodec.createEncoderByType("video/avc");
codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
Surface encoderSurface = codec.createInputSurface();

// 2. 创建 EGLSurface 绑定到 encoderSurface
EGLSurface eglSurface = EGL14.eglCreateWindowSurface(
    eglDisplay, eglConfig, encoderSurface, surfaceAttribs, 0);

// 3. 渲染时: OES 纹理采样 → FBO render → RGBA 纹理 → 渲染到 eglSurface
//    渲染到 eglSurface 的内容 = 写入 MediaCodec 的 input GraphicBuffer
//    零拷贝! MediaCodec 直接拿到 GPU 渲染的 RGBA 数据
//    (编码器内部可能会做一次 RGB→YUV 转换，但那是在编码器硬件内部)

EGL14.eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
// ... 渲染 ...
EGL14.eglSwapBuffers(eglDisplay, eglSurface);
//     ↑ 此时一帧 RGBA 数据已经进了 MediaCodec 的 input buffer
```

---

## 三、完整对比：Android OES vs iOS IOSurface/CVMetalTextureCache

| 维度 | Android OES 纹理 | iOS CVMetalTextureCache |
|------|-----------------|------------------------|
| 底层内存 | `GraphicBuffer` (gralloc) | `IOSurface` (IOKit 内核对象) |
| GL 入口 | `SurfaceTexture` + `GL_TEXTURE_EXTERNAL_OES` | `CVOpenGLESTextureCache` + `GL_TEXTURE_2D` |
| Metal 入口 | N/A（Android 不支持 Metal） | `CVMetalTextureCache` + `MTLTexture` |
| 纹理 target | `GL_TEXTURE_EXTERNAL_OES`（特殊 target） | `GL_TEXTURE_2D`（普通 target） |
| Shader 采样器 | `samplerExternalOES`（特殊采样器） | `sampler2D`（普通采样器） |
| YUV→RGB 时机 | Shader 采样时，由硬件隐式完成 | 你自己在 shader 里写 YUV→RGB 矩阵 |
| Y/UV 纹理分离 | 不分——一张 OES 纹理包含完整 YUV 帧 | 分——NV12 的 Y 和 UV 映射为两张独立纹理 |
| 方向处理 | `getTransformMatrix()` 矩阵，vertex shader 乘 | `CVBufferGetAttachment` 读方向，自己处理 |
| 零拷贝保证 | ✅ EGLImage alias GraphicBuffer | ✅ CVMetalTextureCache alias IOSurface |
| CPU 可读 | ❌ 不能 Lock（除非用 ImageReader 替代 Surface） | ⚠️ 可以 LockBaseAddress（但有 detile 开销） |

---

## 四、常见坑速查

| 现象 | 根因 | 修复 |
|------|------|------|
| OES 纹理采样全黑 | 没调 `updateTexImage()`，纹理还是初始状态 | 在 `onFrameAvailable` 回调后、渲染前调 |
| 画面方向不对 | 没用 `getTransformMatrix()` 或矩阵乘错了 | vertex shader 里把矩阵乘到 texCoord 上 |
| `#extension` 编译报错 | fragment shader 没声明 OES 扩展 | 第一行加 `#extension GL_OES_EGL_image_external : require` |
| sampler2D 采样 OES 纹理编译报错 | OES 纹理必须用 `samplerExternalOES` | 改声明 + 扩展声明 |
| 纹理 wrap GL_REPEAT 无效/报错 | OES 只支持 `GL_CLAMP_TO_EDGE` | 在 shader 或 vertex buffer 里 clamp 坐标 |
| `updateTexImage()` 卡顿 | Producer (Camera) 帧率和 Consumer (GL) 不同步 | 用 `setOnFrameAvailableListener` 按需更新，不要每帧 poll |
| 多个 GL context 共享 OES 纹理 | OES 纹理不能在 context 间共享（EGLImage 绑定是 per-context 的） | 主 context 更新 OES → render-to-texture → 把普通纹理共享给其他 context |
| onFrameAvailable 回调不及时 | SurfaceTexture 用默认参数创建，内部缓冲队列太小 | 创建前调 `setDefaultBufferSize` 设置合适的宽高 |

---

## 五、一句话总结

> OES 纹理是 Android OpenGL ES 为"外部产生的 YUV 内存"开的一扇采样窗口——它通过 `EGLImage` + `glEGLImageTargetTexture2DOES` 把 `GraphicBuffer` 零拷贝映射为 GL 可采样的纹理，让 Camera 预览和 MediaCodec 解码输出能直接进 GPU 渲染管线，全程 CPU 不碰像素数据。代价是必须用 `samplerExternalOES`、不能做 FBO 渲染目标、不能 mipmap、只能 clamp——但换来了每帧节省数 MB 的 CPU 拷贝。

---

## 六、自检清单

- `GL_TEXTURE_EXTERNAL_OES` 和 `GL_TEXTURE_2D` 的本质区别是什么？为什么需要两个不同的 target？
- `updateTexImage()` 内部做了什么？每一步对应的 API 是什么？
- `EGLImage` 在 OES 纹理的零拷贝链路中起什么作用？
- `getTransformMatrix()` 封装了哪些变换？在 shader 的哪个阶段应用？
- 为什么 OES 纹理不能用 `sampler2D`？如果在 OES 纹理上用 `sampler2D` 会发生什么？
- OES 纹理为什么不能作为 FBO 的渲染目标？
- 从 OES 纹理到 MediaCodec 编码器，数据经历了哪些步骤？哪里是零拷贝？
- Android OES 和 iOS CVMetalTextureCache 在架构上的核心差异是什么？
- `onFrameAvailable` 回调在哪个线程？为什么不能在里面直接调 GL 操作？
- 如果 Camera 出的是 1920×1080 NV12，但 Surface 设了 1280×720，OES 纹理采样出来的尺寸是多少？
