# EffectHandle 输出路径零拷贝优化

> 模块: `VirtualCameraEffects/src/EffectHandle.{h,mm}`
> 影响范围: macOS camera-extension 美颜叠加路径(每帧执行)
> 类型: 性能优化 + 隐藏 bug 修复

---

## 1. 背景

`EffectHandle` 是 ByteDance 美颜 SDK 的 OpenGL 封装,在 `BeautyOverlay::Apply` 中被每帧调用:

```
camera-extension EffectFrameWorker (effect thread)
  └─ VirtualBackgroundTool::HandleEffect
        └─ Pipeline::Process (Strategy::Apply 写完 outframe)
              └─ BeautyOverlay::Apply(outframe, outframe, ts)   ← 后置叠加,in-place
                    └─ EffectHandle::process(...)               ← 本次优化点
```

`EffectHandle::process` 的工作流:

```
inputPixelBuffer (BGRA, IOSurface)
   │ updatePixelBuffer  → glTexImage2D
   ▼
_inputTexture  (GL_TEXTURE_2D)
   │ bef_effect_ai_algorithm_texture / process_texture
   ▼
_outputTexture (GL_TEXTURE_2D)
   │ ←★ 这一步要把 GPU 上的 _outputTexture 写回到 outputPixelbuffer
   ▼
outputPixelbuffer (BGRA, IOSurface)
```

★ 这一步原来是 `glReadPixels`,本次改为 IOSurface 共享 + FBO blit。

---

## 2. 旧实现的问题

### 2.1 性能问题:`glReadPixels` 是同步 GPU→CPU 读回

```cpp
// 旧实现(伪代码)
glBindTexture(GL_TEXTURE_2D, _outputTexture);     // ←★ bug 见 2.2
glPixelStorei(GL_PACK_ALIGNMENT, 1);
glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, pixelBufferData);
```

| 维度 | 量级(1080p@30fps) |
|---|---|
| 单帧数据量 | 1920×1080×4 = ~8 MB |
| 持续带宽 | ~240 MB/s GPU→CPU 单向拷贝 |
| 行为 | `glReadPixels` 强制 GPU flush 所有未完成命令,CPU 同步等待 GPU 完成 |
| 副作用 | 调用线程在驱动里 busy-wait,渲染管线 stall;开美颜后帧率下降明显 |

### 2.2 隐藏 bug:原 `glReadPixels` 实际**没读到目标纹理**

#### 2.2.1 原代码

```cpp
bool CopyTextureToPixelBuffer(CVPixelBufferRef pixelBuffer, GLuint textureName) {
    CVPixelBufferLockBaseAddress(pixelBuffer, 0);
    void *pixelBufferData = CVPixelBufferGetBaseAddress(pixelBuffer);
    size_t width  = CVPixelBufferGetWidth(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);

    // ★ 这里就是 bug
    glBindTexture(GL_TEXTURE_2D, textureName);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, pixelBufferData);

    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
    return true;
}
```

读起来"很自然":"把这个 texture bind 上,然后 read pixels 出来"。直觉上像是从这个 texture 把像素读出来。

**但 OpenGL 规范里这两件事完全没有关系。**

#### 2.2.2 `glBindTexture` 实际做了什么

```cpp
glBindTexture(GL_TEXTURE_2D, textureName);
```

它只做一件事:**把 `textureName` 这个对象绑定到当前 texture unit 的 `GL_TEXTURE_2D` slot**。

这个绑定**只**影响后续这些 API:

| API | 用绑定的 texture 做什么 |
|---|---|
| `glTexImage2D` / `glTexSubImage2D` | 上传/更新这块 texture 的像素 |
| `glTexParameteri` | 改这块 texture 的过滤/wrap 参数 |
| 着色器里 `texture(sampler, ...)` | 采样这块 texture |
| `glGenerateMipmap` | 给这块 texture 生 mipmap |

**没有任何一个"从 texture 读像素"的 API**。OpenGL 不存在 `glReadPixelsFromTexture` 这种东西。

#### 2.2.3 `glReadPixels` 实际做了什么

OpenGL 4.5 规范 18.2.1:

> `ReadPixels` obtains values from the **selected read buffer** ... determined by the buffer specified by `glReadBuffer` for the read framebuffer.

也就是 `glReadPixels` **只**从一个地方读数据:

```
当前绑定的 GL_READ_FRAMEBUFFER 的 selected read buffer
```

跟 `glBindTexture` 完全不沾边。`glBindTexture` 没有 side effect 修改 `GL_READ_FRAMEBUFFER`,所以原代码里那行 `glBindTexture` 对 `glReadPixels` 来说**等于没写**。

#### 2.2.4 那原代码到底读到了什么?

`glReadPixels` 读的是当前 `GL_READ_FRAMEBUFFER`。这个 framebuffer 是谁,要看进入 `CopyTextureToPixelBuffer` 时谁最后绑了它。回到调用链:

```cpp
// EffectHandle::process
updatePixelBuffer(_inputTexture, inputPixelBuffer);
updateTexture(_outputTexture, width, height, nullptr);

bef_effect_ai_set_width_height(_handle, _texWidth, _texHeight);
bef_effect_ai_algorithm_texture(_handle, _inputTexture, timeStamp);
bef_effect_ai_process_texture(_handle, _inputTexture, _outputTexture, timeStamp);
   //       ↑ SDK 内部肯定:
   //         1. 建一个 FBO
   //         2. 把 _outputTexture attach 到 COLOR_ATTACHMENT0
   //         3. 设 viewport
   //         4. 跑一系列 shader pass 渲染到这个 FBO
   //         5. 完事...至于绑定状态怎么留,SDK 文档没承诺

CopyTextureToPixelBuffer(outputPixelbuffer, _outputTexture);
   //       ↑ glReadPixels 从"当前 GL_READ_FRAMEBUFFER"读
   //         如果 SDK 没解绑 → 正好读到 _outputTexture(走运)
   //         如果 SDK 解绑了 → 读 default framebuffer(垃圾)
```

所以原代码**能不能读到正确数据,取决于 SDK 内部不保证的状态留存**。

#### 2.2.5 为什么"看起来"正常

我们给 `_glContext` 用的是:

```objc
_glContext = [[NSOpenGLContext alloc] initWithFormat:pixelFormat shareContext:nil];
// 注意:没有 setView:
```

**没有 drawable** —— 这是一个离屏(offscreen)上下文。这种情况下"default framebuffer"(`GL_FRAMEBUFFER` 0)没有真正的 surface backing,内容是 undefined。`glReadPixels` 从 default framebuffer 读出来在不同驱动上行为不一样:

- 某些驱动:返回 0(全黑),或者上次 SDK 用 default FB 跑 pass 留下的残影
- 某些驱动:GL error,但不抛
- macOS 实测:大概率 SDK 在 `process_texture` 退出前没有 `glBindFramebuffer(GL_READ_FRAMEBUFFER, 0)`,所以读到的恰好是 SDK 那个内部 FBO 上 `_outputTexture` 的内容 —— 这就是"看起来对"的解释

但这是**依赖第三方 SDK 内部 GL 状态留存的未定义行为**:

- SDK 升级换实现可能就坏了
- 不同机型/驱动行为可能不一样
- 走 `bef_effect_ai_*` 之外的代码路径(比如 `_noProcessEffect = YES` 时直接走 `render` 不调 `process`)状态完全不同

#### 2.2.6 正确写法

要从 texture 读像素,**唯一**的官方做法:

1. 建一个 FBO
2. 把 texture 用 `glFramebufferTexture2D` attach 到 `GL_COLOR_ATTACHMENT0`
3. 把这个 FBO 绑到 `GL_READ_FRAMEBUFFER`
4. `glReadPixels`

修复后的代码 [VirtualCameraEffects/src/EffectHandle.mm:513-571](VirtualCameraEffects/src/EffectHandle.mm#L513-L571):

```cpp
bool CopyTextureToPixelBuffer(CVPixelBufferRef pixelBuffer, GLuint textureName) {
    // ... lock pixel buffer ...

    // 保存当前 read FBO,函数退出时恢复 —— 不要污染 SDK 状态
    GLint prevReadFBO = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);

    // 建临时 FBO + attach texture
    GLuint tmpFBO = 0;
    glGenFramebuffers(1, &tmpFBO);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, tmpFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, textureName, 0);

    GLint packAlignment;
    glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    // FBO 完整性检查
    GLenum status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
    bool ok = true;
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        ok = false;
    } else {
        // 现在 glReadPixels 真的从 textureName 读了
        glReadPixels(0, 0, (GLsizei)width, (GLsizei)height,
                     GL_BGRA, GL_UNSIGNED_BYTE, pixelBufferData);
        if (glGetError() != GL_NO_ERROR) ok = false;
    }

    // 恢复状态
    glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
    glDeleteFramebuffers(1, &tmpFBO);

    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
    return ok;
}
```

#### 2.2.7 修复前后对比

```
旧代码(spec-wrong,靠 SDK 状态留存运气):

  glBindTexture(GL_TEXTURE_2D, _outputTexture)
        │
        ▼
  绑定到 texture unit (没人用 ← 死代码)

  glReadPixels(...)
        │
        ▼
  读 GL_READ_FRAMEBUFFER (= 任何 SDK 留下的状态)
        │
        ▼
  pixelBufferData (赌赢了就对,赌输了就垃圾)


新代码(spec-correct,行为确定):

  _outputTexture
        │ glFramebufferTexture2D(COLOR_ATTACHMENT0, ...)
        ▼
  tmpFBO ── attach ──> _outputTexture
        │
        │ glBindFramebuffer(GL_READ_FRAMEBUFFER, tmpFBO)
        ▼
  glReadPixels(...) → 一定读到 _outputTexture 的像素
        │
        ▼
  pixelBufferData
```

#### 2.2.8 辨别这类 bug 的方法(经验沉淀)

**症状**:看起来工作正常,但代码语义上错误。常见于:

- API 名字读起来"像"是要做的事(`glBindTexture` 后跟 `glReadPixels`,听起来很顺)
- 但实际行为由规范决定,不由名字决定
- "工作正常"是因为偶然处于某个状态,而非代码本身正确

**预防**:OpenGL 这种**全局状态机** API,不能凭直觉,要查规范。三个核心问题:

1. 这个 API 读/写哪个**全局状态**?(`glBindTexture` 写 texture unit binding;`glReadPixels` 读 `GL_READ_FRAMEBUFFER`)
2. 我修改的状态被哪些后续 API 消费?
3. 后续 API 实际依赖什么状态,我设了吗?

如果问完这三个问题,原代码作者会立刻发现 `glBindTexture` 与 `glReadPixels` 的状态域不重合。

### 2.3 配套问题(顺带改的)

`EffectHandle::currentTime()` 原来用 `std::chrono::seconds`,精度只到秒:

```cpp
// 旧
return std::chrono::duration_cast<std::chrono::seconds>(...).count();
// 新
return std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count() / 1000.0;  // ms
```

`_lastRenderTime` 由它差分得出,旧实现下永远是 0 或 1,导致 `getRenderTime()` 监控指标完全失真。

---

## 3. 新方案:IOSurface 共享内存 + FBO Blit

### 3.1 思路图

```
   旧路径(同步读回,GPU→CPU)             新路径(GPU→GPU,共享内存)
   
   _outputTexture (GPU)                  _outputTexture (GPU, GL_TEXTURE_2D)
         │                                       │ glBlitFramebuffer
         │ glReadPixels                          ▼
         │  (GPU stall +                  IOSurface-backed
         │   240MB/s 拷贝)                RECTANGLE 纹理 (GPU)
         ▼                                       │ 共享 IOSurface 内存
   pixelBufferData (CPU)             ←  ←  CVPixelBuffer
```

关键洞察:macOS 的 `CVPixelBuffer` 如果由带 `kCVPixelBufferIOSurfacePropertiesKey` 的 pool 创建,底层是 **IOSurface** —— 一种**进程间、CPU/GPU 间共享的内存对象**。OpenGL 通过 `CVOpenGLTextureCache` 可以拿到一个**与该 IOSurface 共享存储的纹理**。我们把数据 GPU→GPU blit 进这个纹理,CVPixelBuffer 那侧就同步看到了内容,**完全没有 CPU 拷贝**。

### 3.2 为什么不让 SDK 直接渲染到 IOSurface 纹理

最理想的零拷贝是让 SDK 的 `bef_effect_ai_process_texture` 直接渲染到 IOSurface 纹理。但有两个约束:

1. SDK 用 GLES 3.0,**不支持 `GL_TEXTURE_RECTANGLE`**(IOSurface 纹理通常是 RECTANGLE)
2. SDK 内部假设输出是 `GL_TEXTURE_2D`,改 target 风险高、不可控

所以采用**保守策略**:SDK 输出 target 不动,在末尾加一次 GPU→GPU 的 FBO blit 把 `GL_TEXTURE_2D` 拷到 IOSurface 纹理。这个 blit 在 GPU 内部完成,带宽走显存,不上 PCIe。

### 3.3 上游契约(为什么这条路能走通)

`outputPixelbuffer` 在调用链上的来源都是 IOSurface-backed 的:

[camera-extension/src/EffectFrameWorker.mm:187-194](camera-extension/src/EffectFrameWorker.mm#L187-L194)
```objc
NSDictionary* attributes = @{
    (id)kCVPixelBufferWidthKey : @(_dstWidth.load()),
    (id)kCVPixelBufferHeightKey : @(_dstHeight.load()),
    (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
    (id)kCVPixelBufferIOSurfacePropertiesKey : @{},   // ← 关键
};
CVPixelBufferPoolCreate(..., &_bufferPool);
```

这块 buffer 经 `HandleEffect → Pipeline → BeautyOverlay::Apply` 透传到 `EffectHandle::process`,所以零拷贝路径稳定命中。即便上游换实现没带 IOSurface,代码里有 fallback 到 fixed 后的 `glReadPixels` 路径,不会崩。

---

## 4. 代码改动详解

### 4.1 头文件:加三个成员

[VirtualCameraEffects/src/EffectHandle.h](VirtualCameraEffects/src/EffectHandle.h)

```cpp
private:
    // ... 原有成员 ...

    // IOSurface 零拷贝输出路径资源
    void* _textureCache = nullptr;   // CVOpenGLTextureCacheRef
    GLuint _blitReadFBO = 0;
    GLuint _blitDrawFBO = 0;
```

`_textureCache` 用 `void*` 而不是 `CVOpenGLTextureCacheRef`,目的是避免在头文件里 `import <CoreVideo/CoreVideo.h>` 污染包含此头的 C++ TU。

### 4.2 `initEffect`:创建 cache + 两个 FBO

[VirtualCameraEffects/src/EffectHandle.mm:166-181](VirtualCameraEffects/src/EffectHandle.mm#L166-L181)

```cpp
// IOSurface 零拷贝输出路径资源:texture cache + 两个 FBO 用于 glBlitFramebuffer。
// CVOpenGLTextureCacheCreate 失败不致命,只是回退到 glReadPixels 老路径。
CGLContextObj cglCtx = [_glContext CGLContextObj];
CGLPixelFormatObj cglPF = CGLGetPixelFormat(cglCtx);
CVOpenGLTextureCacheRef cache = NULL;
CVReturn cvret = CVOpenGLTextureCacheCreate(kCFAllocatorDefault, NULL,
                                            cglCtx, cglPF, NULL, &cache);
if (cvret == kCVReturnSuccess && cache) {
    _textureCache = (void*)cache;
    glGenFramebuffers(1, &_blitReadFBO);
    glGenFramebuffers(1, &_blitDrawFBO);
    LOG(INFO) << "EffectHandle: IOSurface zero-copy path enabled";
} else {
    LOG(ERROR) << "CVOpenGLTextureCacheCreate failed: " << cvret
               << ", fallback to glReadPixels path";
}
```

注意点:
- `CVOpenGLTextureCacheCreate` 需要 NSOpenGLContext 已激活,所以放在 `bef_effect_ai_*` 之后(此时 context 一定 current)
- 失败不抛错,降级到兜底路径

### 4.3 `releaseEffect`:对应释放

[VirtualCameraEffects/src/EffectHandle.mm:65-78](VirtualCameraEffects/src/EffectHandle.mm#L65-L78)

```cpp
if (_blitReadFBO) {
    glDeleteFramebuffers(1, &_blitReadFBO);
    _blitReadFBO = 0;
}
if (_blitDrawFBO) {
    glDeleteFramebuffers(1, &_blitDrawFBO);
    _blitDrawFBO = 0;
}
if (_textureCache) {
    CVOpenGLTextureCacheRef cache = (CVOpenGLTextureCacheRef)_textureCache;
    CVOpenGLTextureCacheFlush(cache, 0);
    CFRelease(cache);
    _textureCache = nullptr;
}
```

释放顺序在 `glDeleteTextures` 之后、`unlockOpenGLContext` 之前 —— 必须在 GL context 仍 current 时释放 GL 资源。

### 4.4 核心:`BlitTextureToIOSurfacePixelBuffer`

[VirtualCameraEffects/src/EffectHandle.mm:456-511](VirtualCameraEffects/src/EffectHandle.mm#L456-L511)

```cpp
static bool BlitTextureToIOSurfacePixelBuffer(CVOpenGLTextureCacheRef cache,
                                              GLuint readFBO,
                                              GLuint drawFBO,
                                              GLuint srcTex,
                                              CVPixelBufferRef pixelBuffer,
                                              int width, int height) {
    if (!cache || !pixelBuffer || !readFBO || !drawFBO) return false;
    if (!CVPixelBufferGetIOSurface(pixelBuffer)) return false;

    // 1. 让 cache 给一个与 pixelBuffer 共享 IOSurface 的 GL 纹理
    CVOpenGLTextureRef dstTex = NULL;
    CVReturn ret = CVOpenGLTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault, cache, pixelBuffer, NULL, &dstTex);
    if (ret != kCVReturnSuccess || !dstTex) { ... }

    GLenum dstTarget = CVOpenGLTextureGetTarget(dstTex);  // GL_TEXTURE_RECTANGLE
    GLuint dstName = CVOpenGLTextureGetName(dstTex);

    // 2. 保存当前 FBO 绑定,blit 完恢复 —— 避免影响调用方/SDK 状态
    GLint prevReadFBO = 0, prevDrawFBO = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFBO);

    // 3. 把 src texture (GL_TEXTURE_2D) attach 到 readFBO
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, srcTex, 0);

    // 4. 把 dst texture (RECTANGLE) attach 到 drawFBO
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFBO);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           dstTarget, dstName, 0);

    // 5. 完整性检查 + GPU→GPU blit
    if (read/draw FBO complete) {
        // 1:1 blit,不做 Y 翻转(_outputTexture 与 IOSurface 内存布局一致,
        // row 0 都是图像 top row)
        glBlitFramebuffer(0, 0, width, height,
                          0, 0, width, height,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    // 6. 恢复 FBO 绑定,释放 dstTex,flush cache
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
    CFRelease(dstTex);
    CVOpenGLTextureCacheFlush(cache, 0);
    return ok;
}
```

### 4.5 `process` 末尾:优先零拷贝,失败兜底

[VirtualCameraEffects/src/EffectHandle.mm:637-657](VirtualCameraEffects/src/EffectHandle.mm#L637-L657)

```cpp
// 优先走 IOSurface 零拷贝路径 (FBO blit GPU→GPU);失败兜底 glReadPixels。
bool zero_copy_ok = false;
if (_textureCache) {
    double beginTime = currentTime();
    zero_copy_ok = BlitTextureToIOSurfacePixelBuffer(
        (CVOpenGLTextureCacheRef)_textureCache,
        _blitReadFBO, _blitDrawFBO,
        _outputTexture, outputPixelbuffer,
        _texWidth, _texHeight);
    double endTime = currentTime();
    LOG(INFO) << "EffectHandle: BlitTextureToIOSurfacePixelBuffer time: " << endTime - beginTime;
}
if (!zero_copy_ok) {
    CopyTextureToPixelBuffer(outputPixelbuffer, _outputTexture);
}
```

### 4.6 顺手修的 `CopyTextureToPixelBuffer`

[VirtualCameraEffects/src/EffectHandle.mm:513-571](VirtualCameraEffects/src/EffectHandle.mm#L513-L571)

修复了 2.2 描述的 bug —— `glReadPixels` 必须先把 texture attach 到 FBO 才能读对:

```cpp
// 用临时 FBO 把纹理 attach 到 COLOR_ATTACHMENT0,glReadPixels 才能读到正确内容
// (glReadPixels 读 framebuffer,不读纹理)。
GLuint tmpFBO = 0;
glGenFramebuffers(1, &tmpFBO);
glBindFramebuffer(GL_READ_FRAMEBUFFER, tmpFBO);
glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                       GL_TEXTURE_2D, textureName, 0);
// ... glReadPixels ...
glDeleteFramebuffers(1, &tmpFBO);
```

---

## 5. 走过的坑(分享重点)

### 坑 1:Y 方向翻转的误判

第一版我写了 Y 翻转的 blit:

```cpp
// ❌ 第一版 — 错的
glBlitFramebuffer(0, 0, width, height,
                  0, height, width, 0,    // dst Y 翻转
                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
```

依据是教科书式推理:**"GL_TEXTURE_2D 原点左下,IOSurface 原点左上,所以要 Y 翻"**。

实测画面整个上下颠倒。

**真相**:这两端的内存都是 row-major 存储,**物理上 row 0 都是图像 top row**。OpenGL 只有在**采样到屏幕**时把 row 0 当 bottom 解释。`glReadPixels` 和 `glBlitFramebuffer` 都是按内存索引拷贝,不存在中间的"屏幕坐标"翻转。

```cpp
// ✅ 正确版本 — 1:1 blit
glBlitFramebuffer(0, 0, width, height,
                  0, 0, width, height,
                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
```

经验:**"原点在哪"是 sampler 语义,跟内存布局是两回事**。改 OpenGL 路径时,先看原版做了什么(原版 `glReadPixels` 没翻 Y → 新版也不该翻),再对照规范推。

### 坑 2:`glReadPixels` 读的是 framebuffer,不读 texture

详见 [§2.2](#22-隐藏-bug原-glreadpixels-实际没读到目标纹理)。简言之:`glBindTexture` + `glReadPixels` 不是从 texture 读,`glReadPixels` 永远读 `GL_READ_FRAMEBUFFER`。要从 texture 读,必须 FBO attach。

经验:**API 行为以规范为准,不要凭"读起来像"假设语义**。OpenGL 这种全局状态机 API,认清"我修改的状态会被哪些后续 API 消费"是判断对错的唯一办法。

### 坑 3:FBO binding 要保存恢复

调用方(SDK)可能依赖 FBO binding 状态。我们不能假设进 `process` 时 FBO 绑定是 0,blit 完不恢复就可能让 SDK 下一帧 GL 状态错乱。

```cpp
GLint prevReadFBO = 0, prevDrawFBO = 0;
glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);
glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFBO);
// ... 改 binding 干活 ...
glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
```

经验:**第三方 SDK 嵌入路径的 GL 状态,改完务必复原**。

### 坑 4:`_textureCache` 用 `void*` 头声明

`CVOpenGLTextureCacheRef` 是 CoreVideo 类型,在 .h 引入会污染所有 include 此头的纯 C++ TU。用 `void*` + .mm 内 reinterpret 是 macOS 上常见的 OC type 跨 C++ 边界的处理。

---

## 6. 收益评估

### 6.1 性能

| 指标 | 旧 (`glReadPixels`) | 新 (FBO blit) |
|---|---|---|
| 数据传输方向 | GPU → CPU | GPU → GPU(共享 IOSurface) |
| 1080p 单帧理论数据量 | 8 MB(走 PCIe / 系统总线) | 0(共享内存,DMA 都不需要) |
| 30fps 持续带宽 | ~240 MB/s | ~0 |
| GPU pipeline 行为 | 强制 flush + CPU 等待 | 异步 blit,不 stall |

实测预期:1080p@30fps 美颜开启时,单帧节省 3-8ms(机型/驱动相关);M 系列芯片上由于 unified memory 收益相对较小,Intel 机型差异更大。

### 6.2 正确性

修复了 `CopyTextureToPixelBuffer` 的纹理读取 bug。即使将来零拷贝路径在某种平台/buffer 上 fallback,兜底路径现在也是对的。

### 6.3 鲁棒性

零拷贝失败(无 IOSurface backing / cache create 失败 / FBO incomplete)自动降级,不会崩。

---

## 7. 注意事项 / 后续

- 改 `BeautyOverlay` / `EffectHandle` 上游的 buffer 创建路径时,**保留 `kCVPixelBufferIOSurfacePropertiesKey`**,否则零拷贝路径会 fallback。本次没改 contract,但后续要加 `EffectHandle` 在线程外(非 effect thread)调用要小心:`_textureCache` 不是线程安全的,跟 `_glContext` 一起用 `lockOpenGLContext` 保护即可。
- 当前 LOG 里加了 `BlitTextureToIOSurfacePixelBuffer time` / `CopyTextureToPixelBuffer time` 计时打点 —— 这是优化期临时观察用,merge 前可以考虑改成 N 帧采样一次,避免每帧打 INFO 日志。
- `currentTime()` 改成 ms 后,如果有人依赖 `getRenderTime()` 的旧返回值(秒整数),需要核查;目前仓库里没有调用方依赖具体单位,只用作日志。

---

## 8. 一句话总结

> 把"美颜 SDK 输出 → CVPixelBuffer"从 **GPU→CPU 同步读回**(`glReadPixels`)改为 **GPU→GPU 共享内存 blit**(IOSurface + `glBlitFramebuffer`),消除单帧 8MB 的同步拷贝,并修复一个潜伏的 `glReadPixels` 读错对象的 bug。
