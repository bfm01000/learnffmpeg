# Camera2 视频采集详解：面试速记与原理详解

> **适用方向**：Android 移动端音视频 SDK 开发，拍摄/直播采集方向
> **前置知识**：Android 开发基础（Activity/Handler/TextureView），了解 YUV 颜色空间
> **难度**：⭐⭐⭐（1-5 星）
> **预计阅读**：速记 8 分钟｜全文 30 分钟
> **关联文档**：[[00-Android音视频开发全景导读]] · [[../ffmpeg/14-Android硬件编解码]] · [[04-OpenGLES渲染与Surface详解]]
> **定位**：🟢 中级必会 —— 这是 Android 端视频采集的唯一正统方案（Camera1 已废弃）

---

## 一、全景导读

### 1.1 Camera2 vs Camera1 vs CameraX

| API | 状态 | 适合场景 |
|-----|------|---------|
| **Camera1** (`android.hardware.Camera`) | API 21+ deprecated | 不要用 |
| **Camera2** (`android.hardware.camera2`) | 主流 | 需要精细控制：手动曝光/对焦/RAW/多摄/高速拍摄 |
| **CameraX** (Jetpack) | Google 推荐新项目 | 简单场景（拍照/预览/基础录像），省心但灵活性受限 |

做音视频 SDK 开发（需要逐帧拿到 YUV、控制帧率/分辨率、零拷贝链路） → 必然用 **Camera2**。

### 1.2 Camera2 核心概念

```
CameraManager
  └── CameraDevice (物理摄像头)
        └── CameraCaptureSession (采集会话，默认同时只能有一个)
              ├── CaptureRequest (每次采集的参数)
              └── Surface (输出目标，可以有多个)
                    ├── ImageReader (拿 YUV 到 CPU)
                    ├── SurfaceTexture (→ OpenGL OES 纹理)
                    ├── MediaCodec input Surface (→ 直接编码，零拷贝!)
                    └── MediaRecorder Surface (→ 直接录像)
```

### 1.3 两条数据通路

| 通路 | 输出 | 优点 | 缺点 |
|------|------|------|------|
| **ImageReader** | `Image`（YUV_420_888） | CPU 可读每个 plane 的像素 | 需要手动处理 stride/pixelStride，涉及 GPU→CPU 拷贝 |
| **Surface** | GPU 纹理 | 零拷贝，不经过 CPU | 无法直接读像素（除非用 AHardwareBuffer） |

---

## 二、面试速记

### 2.1 高频考点速查

| # | 考点 | 一句话答案 |
|---|------|-----------|
| 1 | Camera2 怎么搭 | CameraManager.openCamera → createCaptureSession → createCaptureRequest → setRepeatingRequest |
| 2 | YUV_420_888 是什么 | 不保证是 I420/NV12/NV21——取决于设备，必须用 `Image.Plane.rowStride`/`pixelStride` 读 |
| 3 | 怎么拿 YUV 数据 | ImageReader.newInstance(w, h, YUV_420_888, maxImages) → setOnImageAvailableListener |
| 4 | 怎么零拷贝给编码器 | MediaCodec.createInputSurface() → 加进 session 的 surface 列表 → OpenGL 渲染到该 Surface |
| 5 | SurfaceTexture 的作用 | 把 Surface 里的帧转为 OpenGL OES 纹理（updateTexImage） |
| 6 | 丢帧怎么办 | ImageReader 的 maxImages 不能太小（至少 3-4），否则 producer 阻塞或丢帧 |

### 2.2 面试标准回答

#### Q1：Camera2 怎么拿到每一帧 YUV 数据？

> "最快的方式是用 ImageReader。Camera2 的 createCaptureSession 时可以传多个 Surface，ImageReader 就是其中一个。创建时指定 YUV_420_888 格式和 maxImages（通常 3-4 个，太少会导致 producer 阻塞）。setOnImageAvailableListener 在每一帧准备好时回调——注意回调线程是 HandlerThread，不要在主线程处理。回调里通过 `image.getPlanes()` 拿 Y/U/V 三个平面。关键点：不假设 pixelStride=1、不假设 rowStride=width、不假设是 I420 还是 NV12——要根据 Plane 的 rowStride 逐行拷贝有效像素。Image 用完后必须 close()，否则 buffer 被占满后面帧拿不到。"

---

## 三、核心 Demo：Camera2CaptureManager

### 3.1 Camera2CaptureManager.java

```java
// Camera2CaptureManager.java
// 完整的 Android Camera2 视频采集管理器
// 依赖: android.hardware.camera2.*

package com.example.capture;

import android.content.Context;
import android.graphics.ImageFormat;
import android.hardware.camera2.*;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Size;
import android.view.Surface;

import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.Collections;

public class Camera2CaptureManager {

    // ---- 配置 ----
    public interface FrameCallback {
        /** YUV NV21/I420 数据 + 实际宽高 + stride 信息 */
        void onYUVFrame(byte[] yuvData, int width, int height,
                        int yRowStride, int uvRowStride, int uvPixelStride,
                        long timestampNanos);
    }

    // ---- 成员 ----
    private CameraManager cameraManager;
    private CameraDevice cameraDevice;
    private CameraCaptureSession captureSession;
    private ImageReader imageReader;
    private HandlerThread cameraThread;
    private Handler cameraHandler;
    private FrameCallback frameCallback;
    private String cameraId;
    private Size previewSize;

    // ============================================================
    // 初始化
    // ============================================================
    public Camera2CaptureManager(Context context) {
        cameraManager = (CameraManager) context.getSystemService(Context.CAMERA_SERVICE);
        cameraThread = new HandlerThread("CameraThread");
        cameraThread.start();
        cameraHandler = new Handler(cameraThread.getLooper());
    }

    // ============================================================
    // 第一步：选摄像头 + 分辨率
    // ============================================================
    public Size choosePreviewSize(String cameraId, int targetWidth, int targetHeight)
            throws CameraAccessException {
        CameraCharacteristics chars =
            cameraManager.getCameraCharacteristics(cameraId);
        StreamConfigurationMap map =
            chars.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);

        // 选最接近目标分辨率且是 YUV_420_888 支持的 size
        Size[] sizes = map.getOutputSizes(ImageFormat.YUV_420_888);
        Size best = sizes[0];
        for (Size s : sizes) {
            if (s.getWidth() <= targetWidth && s.getHeight() <= targetHeight
                && s.getWidth() >= best.getWidth()) {
                best = s;
            }
        }
        previewSize = best;
        return best;
    }

    // ============================================================
    // 第二步：打开摄像头
    // ============================================================
    public void openCamera(String id, FrameCallback callback) {
        this.cameraId = id;
        this.frameCallback = callback;

        try {
            if (previewSize == null) {
                previewSize = choosePreviewSize(id, 1920, 1080);
            }
            // ★ 申请权限后调用
            cameraManager.openCamera(id, stateCallback, cameraHandler);
        } catch (CameraAccessException | SecurityException e) {
            e.printStackTrace();
        }
    }

    private final CameraDevice.StateCallback stateCallback =
        new CameraDevice.StateCallback() {
            @Override public void onOpened(CameraDevice camera) {
                cameraDevice = camera;
                createCaptureSession();  // ★ 设备打开后立即创建 session
            }
            @Override public void onDisconnected(CameraDevice camera) {
                camera.close();
                cameraDevice = null;
            }
            @Override public void onError(CameraDevice camera, int error) {
                camera.close();
                cameraDevice = null;
            }
        };

    // ============================================================
    // 第三步：创建 CaptureSession + ImageReader
    // ============================================================
    private void createCaptureSession() {
        try {
            // ★ 创建 ImageReader 作为 YUV 数据出口
            imageReader = ImageReader.newInstance(
                previewSize.getWidth(),
                previewSize.getHeight(),
                ImageFormat.YUV_420_888,    // 唯一保证所有设备支持的 YUV 格式
                4                            // maxImages: 至少 3-4, 太少会阻塞
            );

            imageReader.setOnImageAvailableListener(
                imageListener, cameraHandler
            );

            // 把 ImageReader 的 Surface 作为输出目标
            Surface outputSurface = imageReader.getSurface();

            cameraDevice.createCaptureSession(
                Collections.singletonList(outputSurface),
                sessionCallback,
                cameraHandler
            );
        } catch (CameraAccessException e) {
            e.printStackTrace();
        }
    }

    private final CameraCaptureSession.StateCallback sessionCallback =
        new CameraCaptureSession.StateCallback() {
            @Override
            public void onConfigured(CameraCaptureSession session) {
                captureSession = session;
                startPreview();  // session 就绪 → 开始预览
            }
            @Override
            public void onConfigureFailed(CameraCaptureSession session) {
                session.close();
            }
        };

    // ============================================================
    // 第四步：开始采集
    // ============================================================
    private void startPreview() {
        try {
            CaptureRequest.Builder builder =
                cameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);

            // 添加输出 Surface
            builder.addTarget(imageReader.getSurface());

            // ★ 帧率：指定帧率范围
            // Range<Integer> fpsRange = new Range<>(30, 30); // 固定 30fps
            // builder.set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, fpsRange);

            // ★ 自动对焦模式
            builder.set(CaptureRequest.CONTROL_AF_MODE,
                CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO);

            // 循环采集（相当于 Camera1 的 setPreviewCallback）
            captureSession.setRepeatingRequest(
                builder.build(),
                null,       // capture callback（可为 null）
                cameraHandler
            );
        } catch (CameraAccessException e) {
            e.printStackTrace();
        }
    }

    // ============================================================
    // ★ 第五步：每一帧 YUV 的回调（关键！）★
    // ============================================================
    private final ImageReader.OnImageAvailableListener imageListener =
        new ImageReader.OnImageAvailableListener() {
            @Override
            public void onImageAvailable(ImageReader reader) {
                // ★ 这个回调在 cameraHandler 上，注意不能阻塞
                Image image = reader.acquireLatestImage();
                if (image == null) return;

                long timestamp = image.getTimestamp();

                // ★ 解析 YUV 三个平面
                Image.Plane[] planes = image.getPlanes();

                // ---- 平面 0：Y ----
                ByteBuffer yBuffer = planes[0].getBuffer();
                int yRowStride   = planes[0].getRowStride();   // 每行字节数
                int yPixelStride = planes[0].getPixelStride(); // Y 始终为 1

                // ---- 平面 1：U（I420）/ UV（NV12/NV21）----
                ByteBuffer uBuffer = planes[1].getBuffer();
                int uRowStride   = planes[1].getRowStride();
                int uPixelStride = planes[1].getPixelStride(); // NV12=2, I420=1

                // ---- 平面 2：V（I420）/ null（NV12）----
                ByteBuffer vBuffer = (planes.length >= 3)
                    ? planes[2].getBuffer() : null;
                int vRowStride   = (planes.length >= 3)
                    ? planes[2].getRowStride() : uRowStride;
                int vPixelStride = (planes.length >= 3)
                    ? planes[2].getPixelStride() : uPixelStride;

                // ★ 判断格式：两个平面 = NV12/NV21，三个平面 = I420
                boolean isSemiPlanar = (planes.length == 2);
                boolean isI420 = (planes.length == 3);

                // ★ 转换为 NV21（Android 最常用格式）
                int width  = previewSize.getWidth();
                int height = previewSize.getHeight();
                byte[] nv21 = new byte[width * height * 3 / 2];

                // 复制 Y
                copyPlane(yBuffer, nv21, 0, 0, width, height,
                          yRowStride, yPixelStride);

                if (isSemiPlanar) {
                    // NV12 → NV21: UV 互换
                    // NV12: UV UV UV... → NV21: VU VU VU...
                    int uvOffset = width * height;
                    int uvHeight = height / 2;
                    copyAndSwapUV(uBuffer, nv21, uvOffset,
                                  width / 2, uvHeight, uRowStride, uPixelStride);
                } else if (isI420) {
                    // I420 → NV21: 交织 U 和 V
                    int uvOffset = width * height;
                    int uvHeight = height / 2;
                    // copy U
                    copyPlane(uBuffer, nv21, uvOffset, 0,
                              width / 2, uvHeight, uRowStride, uPixelStride);
                    // interleave V
                    copyPlane(vBuffer, nv21, uvOffset + 1, 0,
                              width / 2, uvHeight, vRowStride, vPixelStride);
                    // interleave V with U (step=2)
                    interleaveUV(nv21, uvOffset, width);
                }

                image.close();  // ★ 必须 close！否则 buffer 被占满

                // 回调给业务层
                if (frameCallback != null) {
                    frameCallback.onYUVFrame(nv21, width, height,
                        yRowStride, uRowStride, uPixelStride, timestamp);
                }
            }
        };

    // ============================================================
    // 工具：逐行拷贝像素平面（处理 stride ≠ width 的情况）
    // ============================================================
    private static void copyPlane(ByteBuffer src, byte[] dst,
                                   int dstOffset, int srcOffset,
                                   int width, int height,
                                   int rowStride, int pixelStride) {
        int srcPos = srcOffset;
        int dstPos = dstOffset;

        if (pixelStride == 1) {
            // 逐行拷贝
            for (int row = 0; row < height; row++) {
                src.position(srcPos);
                src.get(dst, dstPos, width);
                srcPos += rowStride;
                dstPos += width;
            }
        } else {
            // pixel stride > 1: 逐个像素拷贝
            for (int row = 0; row < height; row++) {
                for (int col = 0; col < width; col++) {
                    dst[dstPos++] = src.get(srcPos + col * pixelStride);
                }
                srcPos += rowStride;
            }
        }
    }

    private static void copyAndSwapUV(ByteBuffer src, byte[] dst,
                                       int dstOffset, int uvWidth, int uvHeight,
                                       int rowStride, int pixelStride) {
        int srcPos = 0;
        for (int row = 0; row < uvHeight; row++) {
            for (int col = 0; col < uvWidth; col++) {
                int srcIdx = srcPos + col * pixelStride;
                dst[dstOffset + row * uvWidth * 2 + col * 2]     = src.get(srcIdx + 1); // V
                dst[dstOffset + row * uvWidth * 2 + col * 2 + 1] = src.get(srcIdx);     // U
            }
            srcPos += rowStride;
        }
    }

    private static void interleaveUV(byte[] dst, int uvOffset, int width) {
        // 此处省略具体实现：将 I420 分离的 U/V 交织成 NV21 的 VU 交替
    }

    // ============================================================
    // 关闭
    // ============================================================
    public void close() {
        if (captureSession != null) {
            captureSession.close();
            captureSession = null;
        }
        if (cameraDevice != null) {
            cameraDevice.close();
            cameraDevice = null;
        }
        if (imageReader != null) {
            imageReader.close();
            imageReader = null;
        }
        cameraThread.quitSafely();
    }
}
```

### 3.2 使用示例

```java
// ---- 在 Activity 中 ----
Camera2CaptureManager capture = new Camera2CaptureManager(this);

// 选前置摄像头
String cameraId = getFrontCameraId();
capture.choosePreviewSize(cameraId, 1920, 1080);

// 打开并开始采集
capture.openCamera(cameraId, (yuvData, width, height,
    yRowStride, uvRowStride, uvPixelStride, timestamp) -> {
    // 收到 NV21 格式的 YUV 数据
    // 可以喂给 MediaCodec 编码器，或者做美颜处理
    encoder.encode(yuvData, width, height, timestamp);
});
```

### 3.3 关键设计决策

**1. 为什么用 YUV_420_888 而不是指定 NV21？**

YUV_420_888 是 Camera2 保证所有设备支持的**唯一** YUV 格式。指定 NV21/I420 反而可能导致不支持。YUV_420_888 的实际内存布局由设备决定——所以代码必须动态判断 planes 的数量和 pixelStride。

**2. 为什么 maxImages = 4？**

ImageReader 内部是生产者（Camera HAL）→ 消费者（你的回调）的环形缓冲。太少 → 你的回调没处理完，Camera HAL 没有空 buffer 可用，导致丢帧。太多 → 内存占用高而且延迟大。3-4 是经验最优值。

**3. 为什么要在 Image 用完立即 close()？**

不 close 的话这块 buffer 永远不会还给 ImageReader 的池子。4 个 buffer 全被你拿着不还，第五帧就丢掉了。和高通的 `releaseOutputBuffer` 是一个道理。

---

## 四、常见坑

### 坑 1：假设 pixelStride=1, rowStride=width

YUV_420_888 的 U/V plane 的 pixelStride 可能是 2（NV12/NV21 的 UV 交织）或 1（I420）。rowStride 可能大于 width（硬件对齐）。直接用 `width * height` 紧凑拷贝会导致画面错位花屏。

### 坑 2：ImageReader 的 maxImages 太小

设成 1-2 → Camera HAL producer 频繁阻塞等待 → 实际帧率远低于设置的帧率。尤其是开了 HDR/夜景等高计算量模式。

### 坑 3：回调线程处理太重

ImageAvailableListener 跑在 HandlerThread，阻塞会导致后续帧积压。只做 Image → byte[] + close，复杂处理 dispatch 到其他线程。

### 坑 4：Surface 模式特有的方向问题

Camera2 在竖屏设备上，传感器的物理方向是横屏。使用 SurfaceView/TextureView 作为输出时要设置正确的旋转角度。ImageReader 输出不受旋转影响。

---

## 🎯 一句话总结

> Camera2 采集 YUV = ImageReader(YUV_420_888) + 动态判断 planes 数量判断 I420/NV12/NV21 + rowStride/pixelStride 逐 plane 正确拷贝 + 用完后 close()。Surface 通路零拷贝给编码器更优。

## 🔗 关联文档

- [[00-Android音视频开发全景导读]] — Android 媒体栈全景图
- [[02-MediaCodec硬编码实战]] — 采集输出喂给编码器
- [[04-OpenGLES渲染与Surface详解]] — SurfaceTexture 零拷贝渲染
- [[../ffmpeg/14-Android硬件编解码]] — MediaCodec 底层的颜色格式详解
