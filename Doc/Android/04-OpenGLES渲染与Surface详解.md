# OpenGL ES 渲染与 Surface 详解：从 SurfaceTexture 到屏幕

> **适用方向**：Android 视频渲染/滤镜/后处理
> **前置知识**：OpenGL ES 基础（shader / 纹理 / FBO），Surface / SurfaceTexture 概念
> **难度**：⭐⭐⭐（1-5 星）
> **预计阅读**：速记 10 分钟｜全文 30 分钟
> **关联文档**：[[00-Android音视频开发全景导读]] · [[../ffmpeg/14-Android硬件编解码]] · [[02-MediaCodec硬编码实战]]
> **定位**：🟡 高级加分 —— Android 渲染链路的必修课

---

## 一、核心概念关系图

```
Camera2 / MediaCodec
       │
       ▼ output
  [Surface]  ←── Android 的 GPU 生产者→消费者队列句柄
       │
       ▼ 包装
  [SurfaceTexture]  ←── 把 Surface 转为 OpenGL OES 纹理的桥梁
       │
       ▼ updateTexImage()
  [OES Texture]  ←── GL_TEXTURE_EXTERNAL_OES, 内容由外部产生
       │
       ▼ samplerExternalOES
  [OpenGL ES Shader]  ←── fragment shader 里采样 OES 纹理
       │
       ▼ FBO / 默认 framebuffer
  [GLSurfaceView / TextureView / SurfaceView]  ←── 最终显示
```

### 1.1 每一步的底层原理（逐跳拆解）

上图每一根箭头都不是「把像素抄一份传给下一层」，而是**句柄（handle）在生产者与消费者之间流转**。贯穿始终的机制是 **BufferQueue**（详见 [[#6.1 BufferQueue 与 GraphicBuffer：图像帧的真正流转机制|§6.1]]）：一条环形队列，一端写、一端读，队列里排的是 `GraphicBuffer`（真实像素所在的物理内存），而不是拷贝。

**① `Camera2 / MediaCodec ──output──▶ Surface`：生产者写帧**

- `Surface` 是一条 BufferQueue 的**生产者端点**。它对外暴露的核心动作是 `dequeueBuffer()`（从队列拿一块空闲 `GraphicBuffer`）→ 写入像素 → `queueBuffer()`（把写好的 buffer 排进队列）。
- Camera2 的 HAL 或 MediaCodec 的硬件解码器，其输出**直接写进 dequeue 出来的 `GraphicBuffer`**（DMA / GPU 写），因此这一步是**零拷贝**——传到下一层的只是这块 buffer 的句柄。
- 一个 Surface 的最终行为，完全取决于它背后 BufferQueue 的消费者是谁（上屏 / 编码器 / OES 纹理 / CPU），对比见 [[#6.2 Surface 的四种消费者|§6.2]]。

**② `Surface ──包装──▶ SurfaceTexture`：把消费者端接进 GL**

- `new SurfaceTexture(textureId)` 内部**创建了一条 BufferQueue，并把自己注册为消费者端**，同时记住要关联的那个 OES 纹理 id。
- `new Surface(surfaceTexture)` 则取出**同一条队列的生产者端**，包成 Surface 交给 Camera/MediaCodec。
- 所以「Surface ↔ SurfaceTexture」本质是**同一条 BufferQueue 的两头**：一头写、一头读。生产者每 `queueBuffer` 一帧，SurfaceTexture 就收到 `onFrameAvailable` 回调。

**③ `SurfaceTexture ──updateTexImage()──▶ OES Texture`：把 buffer 绑成纹理（不拷贝）**

- 在 **GL 线程**调用 `updateTexImage()` 时发生三件事：
  1. `acquireBuffer()`：从队列取出**最新一帧**的 `GraphicBuffer`（丢弃更旧的，天然做「只显示最新帧」）；
  2. 把该 buffer 包成 **`EGLImage`**，再通过 `glEGLImageTargetTexture2DOES` **绑定到那个 OES 纹理 id** —— 纹理从此「指向」这块物理内存，**没有像素复制**；
  3. 锁存 `getTransformMatrix()` 的变换矩阵（旋转/裁剪，来源见 [[#4.1 为什么 SurfaceTexture 需要 transform matrix？|§4.1]]）。
- 必须在持有 EGL context 的 GL 线程调用，否则纹理绑定无效。

**④ `OES Texture ──samplerExternalOES──▶ Shader`：为什么必须是「外部」纹理**

- 相机/解码器给的 `GraphicBuffer` 格式**不是标准 GL 内部格式**：可能是 YUV（NV12/NV21）、可能是厂商 **tiling** 排布（见 [[#6.3 图像内存排布：stride、plane、tiling|§6.3]]）、也可能是受保护内存。普通 `sampler2D` 读不了。
- `GL_TEXTURE_EXTERNAL_OES` + `samplerExternalOES` 让驱动在**采样这一刻**由硬件隐式完成：YUV→RGB 色彩转换、de-tiling 重排、必要的对齐处理。这正是它要求 `#extension GL_OES_EGL_image_external : require` 的原因，也带来了它的限制（无 mipmap、不能作 FBO 颜色附件，见 [[#4.3 OES 纹理限制|§4.3]]）。
- 一句话：**OES 纹理是「外部产生的、非标准格式的 buffer」在 GL 世界里的采样视图**。

**⑤ `Shader ──▶ FBO / 默认 framebuffer`：像素画到哪里**

- Fragment shader 的输出写入**当前绑定的 framebuffer**：
  - **默认 framebuffer**（`glBindFramebuffer(..., 0)`）= 你的 EGL Window Surface，它本身又是**另一条 BufferQueue 的生产者**，消费者是系统合成器 SurfaceFlinger；
  - **FBO**（自建离屏帧缓冲）= 画到一张普通纹理上，用于多 pass 滤镜链、美颜等中间结果。
- 决定「画到哪张纹理」的是 FBO 的 color attachment。

**⑥ `──▶ GLSurfaceView / TextureView / SurfaceView`：上屏**

- 一帧画完后 `eglSwapBuffers()` 把默认 framebuffer 对应的 `GraphicBuffer` **`queueBuffer` 进上屏 BufferQueue**；
- **SurfaceFlinger** 作为消费者，在 **VSync** 时刻把所有图层（Layer）合成，交给显示硬件（HWC）扫描输出到屏幕。
- 到这里，一帧从「相机/解码器产生」到「屏幕点亮」，全程流转的都是 `GraphicBuffer` 句柄，理想路径下**无一次 CPU 像素拷贝**。

> 一句话串起来：**每一跳都是「生产者把 GraphicBuffer 排进一条 BufferQueue，消费者取出来接着用」；Surface 是生产端句柄，SurfaceTexture / SurfaceFlinger / 编码器是不同的消费者，OES 纹理只是让 GL 能采样这块外部 buffer 的「视图」。**

---

## 二、面试速记

| # | 考点 | 一句话答案 |
|---|------|-----------|
| 1 | SurfaceTexture 干什么 | 把 Surface（Camera/MediaCodec 输出）转为 GL OES 纹理，`updateTexImage()` 更新到最新帧 |
| 2 | OES 纹理和普通纹理有什么区别 | 采样器用 `samplerExternalOES` 而不是 `sampler2D`，shader 需要 `#extension GL_OES_EGL_image_external : require` |
| 3 | GLSurfaceView vs TextureView | GLSurfaceView 有独立渲染线程（性能好）、TextureView 可做 View 动画（灵活） |
| 4 | 怎么创建 EGL 上下文 | eglInitialize → eglChooseConfig → eglCreateContext → eglCreateWindowSurface → eglMakeCurrent |
| 5 | #extension GL_OES_EGL_image_external 什么用 | 声明使用 OES 纹理扩展，只能用 samplerExternalOES，纹理坐标不需要 matrix 变换（SurfaceTexture 已处理） |
| 6 | SurfaceTexture 的 transform matrix | getTransformMatrix() 拿到旋转/裁剪/缩放矩阵，vertex shader 里乘到坐标上 |

---

## 三、核心 Demo：GL 渲染器

### 3.1 GL 渲染管线

```java
// GLRenderer.java
// OpenGL ES YUV→RGB 渲染器 (用于 Camera2/MediaCodec Surface 输出)
package com.example.render;

import android.graphics.SurfaceTexture;
import android.opengl.*;
import android.view.Surface;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;

public class GLRenderer implements GLSurfaceView.Renderer {

    // ---- Shader ----
    private static final String VERTEX_SHADER =
        "uniform mat4 uMVPMatrix;\n" +
        "uniform mat4 uTexMatrix;\n" +        // ★ SurfaceTexture 的变换矩阵
        "attribute vec4 aPosition;\n" +
        "attribute vec4 aTexCoord;\n" +
        "varying vec2 vTexCoord;\n" +
        "void main() {\n" +
        "  gl_Position = uMVPMatrix * aPosition;\n" +
        "  vTexCoord = (uTexMatrix * aTexCoord).xy;\n" +
        "}\n";

    // ★ OES 纹理的 fragment shader (NV12→RGB)
    // Y 平面: .r, UV 平面: .rg, 需要两张纹理
    // 这里简化: 单纹理 NV21→RGB (实际工程可能需要双纹理)
    private static final String FRAGMENT_SHADER_NV21 =
        "#extension GL_OES_EGL_image_external : require\n" +
        "precision mediump float;\n" +
        "uniform samplerExternalOES sTexture;\n" +  // ★ OES 采样器
        "varying vec2 vTexCoord;\n" +
        "void main() {\n" +
        "  vec3 yuv;\n" +
        "  vec3 rgb;\n" +
        // 采样 NV21 的 Y (简化，实际需要 Y 和 UV 分开处理)
        "  yuv.x = texture2D(sTexture, vTexCoord).r;\n" +
        // BT.601 Full Range YUV→RGB
        "  yuv.x = yuv.x;\n" +
        "  yuv.y = texture2D(sTexture, vTexCoord).g - 0.5;\n" +
        "  yuv.z = texture2D(sTexture, vTexCoord).b - 0.5;\n" +
        "  rgb = mat3(1.0,1.0,1.0, 0.0,-0.344,-1.772, 1.402,0.714,0.0) * yuv;\n" +
        "  gl_FragColor = vec4(rgb, 1.0);\n" +
        "}\n";

    // 全屏矩形顶点
    private static final float[] VERTICES = {
        -1, -1, 0, 0, 1,   // 左下
         1, -1, 0, 1, 1,   // 右下
        -1,  1, 0, 0, 0,   // 左上
         1,  1, 0, 1, 0,   // 右上
    };
    private FloatBuffer vertexBuffer;

    private int program;
    private int aPosition, aTexCoord;
    private int uMVPMatrix, uTexMatrix, sTexture;

    private SurfaceTexture surfaceTexture;
    private Surface surface;
    private int textureId;
    private float[] texMatrix = new float[16];

    // ---- frame 同步 ----
    private final Object frameLock = new Object();
    private boolean frameAvailable;

    // ============================================================
    // GLSurfaceView.Renderer
    // ============================================================
    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        // 编译 shader
        program = createProgram(VERTEX_SHADER, FRAGMENT_SHADER_NV21);
        aPosition  = glGetAttribLocation(program, "aPosition");
        aTexCoord  = glGetAttribLocation(program, "aTexCoord");
        uMVPMatrix = glGetUniformLocation(program, "uMVPMatrix");
        uTexMatrix = glGetUniformLocation(program, "uTexMatrix");
        sTexture   = glGetUniformLocation(program, "sTexture");

        // 创建 OES 纹理
        int[] textures = new int[1];
        GLES20.glGenTextures(1, textures, 0);
        textureId = textures[0];
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureId);
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR);
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR);
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE);
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE);

        // ★ 创建 SurfaceTexture → 包装成 Surface
        surfaceTexture = new SurfaceTexture(textureId);
        surfaceTexture.setOnFrameAvailableListener(st -> {
            synchronized (frameLock) { frameAvailable = true; }
        });
        surface = new Surface(surfaceTexture);

        // 初始化顶点缓冲
        vertexBuffer = ByteBuffer.allocateDirect(VERTICES.length * 4)
            .order(ByteOrder.nativeOrder()).asFloatBuffer().put(VERTICES);
        vertexBuffer.position(0);
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        synchronized (frameLock) {
            if (frameAvailable) {
                // ★ 更新 OES 纹理到最新帧
                surfaceTexture.updateTexImage();
                // ★ 拿变换矩阵
                surfaceTexture.getTransformMatrix(texMatrix);
                frameAvailable = false;
            }
        }

        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
        GLES20.glUseProgram(program);

        // 绑定 OES 纹理
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureId);
        GLES20.glUniform1i(sTexture, 0);

        // ★ 传变换矩阵
        GLES20.glUniformMatrix4fv(uTexMatrix, 1, false, texMatrix, 0);
        GLES20.glUniformMatrix4fv(uMVPMatrix, 1, false,
            new float[]{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}, 0);

        // 绑定顶点
        GLES20.glEnableVertexAttribArray(aPosition);
        GLES20.glEnableVertexAttribArray(aTexCoord);
        vertexBuffer.position(0);
        GLES20.glVertexAttribPointer(aPosition, 3, GLES20.GL_FLOAT, false, 5*4, vertexBuffer);
        vertexBuffer.position(3);
        GLES20.glVertexAttribPointer(aTexCoord, 2, GLES20.GL_FLOAT, false, 5*4, vertexBuffer);

        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);

        GLES20.glDisableVertexAttribArray(aPosition);
        GLES20.glDisableVertexAttribArray(aTexCoord);
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        GLES20.glViewport(0, 0, width, height);
    }

    // ---- 获取 Surface ----
    public Surface getSurface() { return surface; }

    // ---- Shader 编译工具 ----
    private int createProgram(String vs, String fs) { /* compile+link, return program */ return 0; }
    private int glGetAttribLocation(int p, String n) { return GLES20.glGetAttribLocation(p, n); }
    private int glGetUniformLocation(int p, String n) { return GLES20.glGetUniformLocation(p, n); }
}
```

### 3.2 使用示例

```java
// ---- 在 Activity 中 ----
GLSurfaceView glSurfaceView = findViewById(R.id.gl_surface);
glSurfaceView.setEGLContextClientVersion(2);

GLRenderer renderer = new GLRenderer();
glSurfaceView.setRenderer(renderer);

// ---- 拿 Surface 给 Camera2/MediaCodec ----
Surface renderSurface = renderer.getSurface();

// Camera2 输出到这个 Surface:
captureRequestBuilder.addTarget(renderSurface);

// 或 MediaCodec 解码输出到这个 Surface:
MediaCodecDecoder decoder = new MediaCodecDecoder(renderSurface, callback);
```

---

## 四、关键设计决策

### 4.1 为什么 SurfaceTexture 需要 transform matrix？

Camera 传感器的物理方向是横屏，而且前后摄像头方向不同。SurfaceTexture 的 `getTransformMatrix()` 返回一个包含旋转/缩放的矩阵。**这个矩阵必须乘到 vertex shader 的纹理坐标上**，否则画面方向不对。如果不乘这个矩阵直接采样，前置摄像头画面是镜像反的、后置可能旋转 90°。

### 4.2 GLSurfaceView vs TextureView

| 维度 | GLSurfaceView | TextureView |
|------|--------------|-------------|
| 渲染线程 | 独立 GL 线程 | 硬件加速的 View 绘制线程 |
| 性能 | 更好（专用线程，不争主线程） | 可能差于 GLSurfaceView |
| 动画/Transform | 不能做标准 View 动画 | 可以做 translation/scale/alpha |
| 适合场景 | 全屏视频/游戏 | 需要 View 动画的嵌入场景 |

### 4.3 OES 纹理限制

- **必须用 `samplerExternalOES`** 采样，不能用 `sampler2D`
- **shader 开头必须声明** `#extension GL_OES_EGL_image_external : require`
- **不支持 mipmap**、不支持 repeat wrap mode
- **只支持 GL_TEXTURE_EXTERNAL_OES target**，不能用于 FBO 的 color attachment

---

## 五、SurfaceTexture + EGL 手动管理（无 GLSurfaceView）

如果不想用 GLSurfaceView（比如需要在自定义线程控制渲染节奏），可以手写 EGL：

```java
// ---- 创建 EGL ----
EGLDisplay display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
EGL14.eglInitialize(display, null, 0);

int[] attrs = { EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT, EGL14.EGL_NONE };
EGLConfig[] configs = new EGLConfig[1];
EGL14.eglChooseConfig(display, attrs, 0, configs, 0, 1, null, 0);

EGLContext context = EGL14.eglCreateContext(display, configs[0],
    EGL14.EGL_NO_CONTEXT, new int[]{EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE}, 0);

EGLSurface eglSurface = EGL14.eglCreateWindowSurface(display, configs[0],
    surfaceTexture /* from SurfaceView */, new int[]{EGL14.EGL_NONE}, 0);

EGL14.eglMakeCurrent(display, eglSurface, eglSurface, context);

// ---- 渲染循环 ----
while (rendering) {
    // ... draw ...
    EGL14.eglSwapBuffers(display, eglSurface);
}

// ---- 销毁 ----
EGL14.eglDestroySurface(display, eglSurface);
EGL14.eglDestroyContext(display, context);
EGL14.eglTerminate(display);
```

---

## 六、底层渲染知识扩展

### 6.1 BufferQueue 与 GraphicBuffer：图像帧的真正流转机制

`Surface`、`SurfaceTexture`、`OES Texture` 这些概念背后，真正管理 buffer 流转的是 **BufferQueue**——Android 图形系统的生产者-消费者缓冲队列：

```text
Producer ──→ Surface ──→ BufferQueue ──→ SurfaceTexture ──→ OES Texture ──→ Shader
                (写入口)    (排队/同步/复用)    (消费者适配器)     (GL 采样视图)
```

关键理解：
- **GraphicBuffer**：底层真实图像数据所在的缓冲区。它可以被 GPU、显示硬件、编解码器直接访问，但不是某个 GL 纹理对象。
- **Surface**：站在 BufferQueue 的**生产者侧**，提供「写帧」入口
- **SurfaceTexture**：站在 BufferQueue 的**消费者侧**，负责「接帧」+ 关联为 OES Texture
- **BufferQueue** 由系统图形栈维护，应用层不直接操作，而是通过 Surface/SurfaceTexture 间接使用

**GraphicBuffer vs AHardwareBuffer vs EGLImage vs Texture vs FBO 的分工：**

| 对象 | 角色 | 一句话 |
|---|---|---|
| **GraphicBuffer** | 真实数据载体 | "货物本体" |
| **AHardwareBuffer** | NDK 公开接口 | "系统对外暴露的硬件 buffer 句柄" |
| **EGLImage** | 跨 EGL/GL 的桥 | "把底层 buffer 接进 GL 世界" |
| **Texture** | GL 采样视图 | "Shader 看到的图像" |
| **FBO** | 渲染目标容器 | "决定画到哪张纹理上" |

数据流：`AHardwareBuffer → EGLImage → Texture → FBO 写入`

### 6.2 Surface 的四种消费者

一个 `Surface` 的行为完全取决于它的消费者是谁：

| 来源 | 消费者 | 用途 | 性能 |
|---|---|---|---|
| `SurfaceView` / `TextureView` | SurfaceFlinger | 上屏显示 | 标准路径 |
| `MediaCodec.createInputSurface()` | 硬件编码器 | 录制/推流 | **零拷贝** |
| `new Surface(SurfaceTexture)` | OES 纹理 | 二次渲染/美颜 | **零拷贝**（GPU 内部） |
| `ImageReader.getSurface()` | CPU ByteBuffer | 截帧/CPU 算法 | **极慢**（GPU→CPU 拷贝） |

### 6.3 图像内存排布：stride、plane、tiling

这三个概念是理解「为什么同一块物理内存对 CPU 和 GPU 代价不同」的关键：

**stride**：一行图像在内存中实际占多少字节，通常大于 `width × bytesPerPixel`（因对齐要求）。行地址 = `base + y × stride`。

**plane**：不同分量分开存储。YUV planar：Y plane 独立，UV plane 独立（NV12 是 Y + UV 交错 = semi-planar）。视频链路喜欢 YUV plane（省带宽），CPU 处理比线性 RGBA 更复杂。

**tiling**：图像按二维小块（如 4×4 tile）存储而非按行线性排列。Morton/Z-order 排列让二维邻近像素在物理内存上也邻近：
```
linear:  00 01 02 03 | 10 11 12 13 | ...     ← CPU 喜欢（顺序扫）
tiled:   [TileA: 00 01/10 11] [TileB: 02 03/12 13]  ← GPU/VPU 喜欢（空间局部性）
```
- CPU 喜欢 linear：顺序扫描、Cache Line 友好、地址计算简单
- GPU 喜欢 tiled：双线性采样只需拉一个 tile 进 L1 Cache
- VPU 也喜欢 tiled：H.264 运动搜索按 16×16 宏块进行

**关键结论：不只是「同一块物理内存」就够了——格式、stride、plane、tiling、同步状态，决定了当前访问者能不能高效使用。**

### 6.4 GPU 同步：`glFinish` vs `glFlush` vs Fence

| API | 行为 | CPU 阻塞？ | 适用场景 |
|---|---|---|---|
| `glFlush` | 确保命令尽快送入 GPU 队列 | 不阻塞 | 正常异步渲染 |
| `glFinish` | 等 GPU 执行完**所有**已提交命令 | **阻塞，等全部** | 调试/离线导出 |
| `glFenceSync` + `glWaitSync` | 细粒度同步：只等某个里程碑之前的命令 | `Wait` 不阻塞 CPU（阻塞在 GPU 侧） | 实时预览、跨 context 共享纹理 |

**跨 context 共享纹理时必须同步**：context A 写完纹理 → context B 读之前，需要 `glFenceSync` + `glWaitSync`（或兜底 `glFinish`），否则消费者可能在 GPU 上采样到未写完的数据（花屏）。同 context 串行滤镜链一般不需要额外插 fence。

**`glWaitSync` 不阻塞 CPU**：它只是在 GPU 命令队列里插一条依赖——「后面这条绘制必须先等 fence signaled」。CPU 立刻返回继续干活。

### 6.5 `glReadPixels` 优化演进史（三代方案）

**优化前（阶段零）：** `glReadPixels` 直接读回 CPU 内存 → 4K 单帧 20-40ms。三重代价：管线同步 + De-tiling 重组 + 30MB 内存搬运。

**阶段一：PBO 异步回读**
- GPU 把像素读到 PBO（显存/共享内存缓冲区），CPU 下一帧再 `glMapBuffer` 读回
- CPU 阻塞从 20ms → 1-2ms（异步化 + 流水线重叠）
- 局限：拿回的仍是 RGBA（30MB），还要 CPU 跑 libyuv 转 YUV

**阶段二：GPU Shader 格式转换 + PBO**
- Fragment Shader 在 GPU 内部把 RGBA 实时转 YUV420P
- 数据量砍半（30MB→10.5MB），回读时间再减半
- 局限：数据仍然「GPU→CPU→VPU」折返跑

**阶段三（终极）：AHardwareBuffer / Surface 直通**
- GPU 画进 `AHardwareBuffer`（`GPU_FRAMEBUFFER | VIDEO_ENCODE`）
- 只传句柄不搬数据 → MediaCodec 直接读 → **物理级零拷贝**
- 彻底消除「GPU→CPU→VPU」折返跑，内存总线带宽浪费降为 0
- 持续推流不再因内存带宽过热而降频

### 6.6 双线程异步渲染架构（根治 VSync 卡顿）

当 `eglSwapBuffers` 受 VSync/系统反压产生 30ms+ 长尾阻塞时，单线程无论用 `glFinish` 还是 fence 都无法解决。方案是**双线程 + 共享上下文**：

1. **渲染线程**：绑定 Pbuffer Surface（1×1 dummy），专心跑算法 → 渲染到 FBO Texture → `glFlush()` → 把 Texture ID 塞入同步队列 → **立刻返回**，不调 `eglSwapBuffers`
2. **上屏线程**：绑定真实 Window Surface，通过 `share_context` 共享渲染线程的纹理 → 阻塞取队列 → 画全屏矩形 → `eglSwapBuffers`

**关键**：所有 VSync 阻塞被吸收到上屏线程——它卡死也不阻塞渲染线程的算法进度。这是根治 VSync 卡顿、实现稳定 30/60fps 实时渲染的行业标准解法。

---

## 🎯 一句话总结

> Android 渲染 = Surface(外部帧源) → SurfaceTexture(OES 纹理桥梁) → `samplerExternalOES` shader → YUV→RGB → GLSurfaceView/TextureView。OES 纹理的限制（必须用 samplerExternalOES、无 mipmap）+ SurfaceTexture 的 transform matrix 是最容易忽略的细节。

## 🔗 关联文档

- [[02-MediaCodec硬编码实战]] — Surface 输入零拷贝编码 + AHardwareBuffer 附录
- [[03-MediaCodec硬解码实战]] — Surface 输出零拷贝解码 + 硬解 AVFrame 原理
- [[06-端到端采集编码推流管线]] — 跨平台 RHI 架构 + iOS vs Android 零拷贝对比
