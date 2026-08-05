// =============================================================================
// Q1 代码示例：跨平台音视频 SDK 三层架构
// 场景：AR 眼镜 Camera 采集 → 编码 → 渲染/推流
// =============================================================================

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// =============================================================================
// 第一层：Platform Abstraction Layer（PAL）
// 每个平台实现自己的子类：Android(Camera2/MediaCodec/EGL)、Linux(V4L2/VAAPI/GLX)、Windows(MMF/D3D)
// =============================================================================

// --- 通用数据类型（纯 C++，无平台依赖）---
enum class PixelFormat { YUV420p, NV12, NV21, RGBA8888, BGRA8888 };
enum class CodecID { H264, H265, AAC };
enum class AudioFormat { PCM_S16LE, PCM_F32LE };

struct VideoFrame {
    int width, height, stride;
    PixelFormat format;
    uint8_t* data[4];           // 多平面指针（Y/U/V 或 RGBA 单平面）
    int linesize[4];
    int64_t pts_us;             // 曝光中心时刻，单位 μs
    void* opaque;               // 平台私有 handle（如 CVPixelBufferRef / ANativeWindowBuffer*）
};

struct AudioFrame {
    AudioFormat format;
    int sampleRate, channels, numSamples;
    uint8_t* data;
    int64_t pts_us;
};

struct EncodedPacket {
    CodecID codec;
    uint8_t* data;
    size_t size;
    int64_t pts_us, dts_us;
    bool isKeyFrame;
};

// --- PAL 抽象接口（= 平台必须实现的"合同"）---
class IVideoCapture {
public:
    virtual ~IVideoCapture() = default;
    virtual bool open(int width, int height, int fps) = 0;
    virtual void setFrameCallback(std::function<void(const VideoFrame&)> cb) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void close() = 0;
};

class IAudioCapture {
public:
    virtual ~IAudioCapture() = default;
    virtual bool open(int sampleRate, int channels) = 0;
    virtual void setFrameCallback(std::function<void(const AudioFrame&)> cb) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void close() = 0;
};

class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;
    virtual bool open(int width, int height, int fps, int bitrate, CodecID codec) = 0;
    virtual void setPacketCallback(std::function<void(const EncodedPacket&)> cb) = 0;
    virtual void feedFrame(const VideoFrame& frame) = 0;
    virtual void requestKeyFrame() = 0;     // 弱网/重连时强制出 IDR
    virtual void close() = 0;
};

class IVideoRenderer {
public:
    virtual ~IVideoRenderer() = default;
    virtual bool open(void* nativeWindow) = 0;   // ANativeWindow* / HWND / GLXWindow
    virtual void renderFrame(const VideoFrame& frame) = 0;
    virtual void close() = 0;
};

// =============================================================================
// 第二层：Core Pipeline（纯 C++，不依赖平台 API）
// =============================================================================

struct PipelineConfig {
    // 采集
    int captureWidth = 1920, captureHeight = 1080, captureFps = 30;
    // 编码
    CodecID codec = CodecID::H264;
    int bitrate = 4000000;          // 4Mbps
    bool enablePreview = true;      // AR 眼镜上通常始终 true
    // 算法
    std::vector<std::string> algoModules = {"face_detect", "gesture"}; // 插件名
};

class Pipeline {
public:
    Pipeline(const PipelineConfig& cfg);
    ~Pipeline();

    // --- 生命周期 ---
    bool prepare();                 // open 采集/编码/渲染，失败回滚
    void start();                   // 所有模块开始数据流
    void stop();
    void release();

    // --- 算法注册（运行时动态加载）---
    // 每个算法模块拿到每帧 VideoFrame，异步处理后通过 callback 回传结果
    using AlgoCallback = std::function<void(const std::string& moduleName, void* result)>;
    void registerAlgorithm(const std::string& name, AlgoCallback cb);

    // --- 外部帧注入（比如 Unity 侧自己管理 Camera，只把帧喂给 SDK 编码）---
    void pushVideoFrame(const VideoFrame& frame);
    void pushAudioFrame(const AudioFrame& frame);

    // --- 事件回调（给上层 App/引擎）---
    // 编码输出（给推流模块或本地录制）
    std::function<void(const EncodedPacket&)> onEncodedPacket;
    // 预览帧（给引擎做自定义渲染）
    std::function<void(const VideoFrame&)> onPreviewFrame;
    // 错误
    std::function<void(int code, const std::string& msg)> onError;
    // 算法结果汇总
    std::function<void(const std::string& algoName, void* result)> onAlgoResult;

    // --- 动态控制 ---
    void requestKeyFrame();
    void setBitrate(int bitrate);   // ABR 动态码率

private:
    PipelineConfig m_cfg;
    // PAL 实例——由工厂函数根据平台创建
    std::unique_ptr<IVideoCapture>  m_videoCap;
    std::unique_ptr<IAudioCapture>  m_audioCap;
    std::unique_ptr<IVideoEncoder>  m_videoEnc;
    std::unique_ptr<IVideoRenderer> m_renderer;

    // 内部线程（Pipeline 自己管理，上层不用管）
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

// =============================================================================
// 第三层：C Binding Layer（Unity [DllImport] / UE / JNI 通过这个调用）
// =============================================================================
#ifdef __cplusplus
extern "C" {
#endif

// --- Pipeline ---
typedef void* XRPipelineHandle;

// 配置用 JSON 字符串传入，避免 C struct 版本兼容问题
XRPipelineHandle xr_pipeline_create(const char* jsonConfig);
void             xr_pipeline_destroy(XRPipelineHandle h);

int  xr_pipeline_prepare(XRPipelineHandle h);   // 0=ok
void xr_pipeline_start(XRPipelineHandle h);
void xr_pipeline_stop(XRPipelineHandle h);

// 外部帧注入
void xr_push_video_frame(XRPipelineHandle h,
    int width, int height, int stride, int format,
    const uint8_t* data, int64_t pts_us);

// --- 回调注册 ---
// 回调签名: void callback(void* userData, const uint8_t* data, int size, int64_t pts);

typedef void (*XREncodedPacketCallback)(void* userData, const uint8_t* data, int size,
                                        int isKeyFrame, int64_t pts_us, int64_t dts_us);

void xr_set_encoded_packet_callback(XRPipelineHandle h,
    XREncodedPacketCallback cb, void* userData);

typedef void (*XRPreviewFrameCallback)(void* userData,
    int width, int height, int stride, int format,
    const uint8_t* data, int64_t pts_us);

void xr_set_preview_frame_callback(XRPipelineHandle h,
    XRPreviewFrameCallback cb, void* userData);

typedef void (*XRErrorCallback)(void* userData, int code, const char* msg);
void xr_set_error_callback(XRPipelineHandle h, XRErrorCallback cb, void* userData);

// --- 动态控制 ---
void xr_request_key_frame(XRPipelineHandle h);
void xr_set_bitrate(XRPipelineHandle h, int bitrate);

// --- 算法 ---
void xr_register_algorithm(XRPipelineHandle h, const char* algoName);

typedef void (*XRAlgoResultCallback)(void* userData, const char* algoName,
                                     const uint8_t* result, int resultSize);
void xr_set_algo_result_callback(XRPipelineHandle h, XRAlgoResultCallback cb, void* userData);

// --- 版本 ---
const char* xr_get_version(void);
int         xr_get_version_major(void);
int         xr_get_version_minor(void);

#ifdef __cplusplus
}
#endif

// =============================================================================
// 使用示例 1：原生 C++ App（Android/Linux/Windows 通用）
// =============================================================================
#if 0
int main() {
    PipelineConfig cfg;
    cfg.captureWidth  = 1920;
    cfg.captureHeight = 1080;
    cfg.captureFps    = 30;
    cfg.bitrate       = 4000000;

    Pipeline pipeline(cfg);

    // 注册回调
    pipeline.onEncodedPacket = [](const EncodedPacket& pkt) {
        // 喂给推流模块，或本地写 MP4
        rtmp_send(pkt.data, pkt.size, pkt.pts_us);
    };
    pipeline.onError = [](int code, const std::string& msg) {
        LOGE("Pipeline error [%d]: %s", code, msg.c_str());
    };

    pipeline.prepare();
    pipeline.start();

    // ... App 运行 ...

    pipeline.stop();
    pipeline.release();
}
#endif

// =============================================================================
// 使用示例 2：Unity C# 侧（通过 [DllImport] 调 C API）
// =============================================================================
#if 0
// --- XRSDK.cs ---
using System;
using System.Runtime.InteropServices;

public class XRSDK {
    [DllImport("XRSDK")]
    public static extern IntPtr xr_pipeline_create(string jsonConfig);

    [DllImport("XRSDK")]
    public static extern int xr_pipeline_prepare(IntPtr h);

    [DllImport("XRSDK")]
    public static extern void xr_pipeline_start(IntPtr h);

    [DllImport("XRSDK")]
    public static extern void xr_pipeline_stop(IntPtr h);

    [DllImport("XRSDK")]
    public static extern void xr_pipeline_destroy(IntPtr h);

    // 回调委托
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void EncodedPacketCallback(IntPtr userData, IntPtr data,
        int size, int isKeyFrame, long ptsUs, long dtsUs);

    [DllImport("XRSDK")]
    public static extern void xr_set_encoded_packet_callback(IntPtr h,
        EncodedPacketCallback cb, IntPtr userData);

    // ... 其他 API 类似 ...
}

// --- Unity MonoBehaviour 里用 ---
public class ARStreamer : MonoBehaviour {
    IntPtr m_pipeline;

    void Start() {
        string json = @"{""captureWidth"":1920,""captureHeight"":1080,""fps"":30}";
        m_pipeline = XRSDK.xr_pipeline_create(json);
        XRSDK.xr_pipeline_prepare(m_pipeline);

        // ★ 关键：回调在 native 线程触发，通过 GL.IssuePluginEvent 抛回 Unity 渲染线程
        XRSDK.xr_set_encoded_packet_callback(m_pipeline, OnEncodedPacket, IntPtr.Zero);

        XRSDK.xr_pipeline_start(m_pipeline);
    }

    [AOT.MonoPInvokeCallback(typeof(XRSDK.EncodedPacketCallback))]
    static void OnEncodedPacket(IntPtr userData, IntPtr data, int size,
                                 int isKeyFrame, long ptsUs, long dtsUs) {
        // ★ 在 native 线程！不能调 Unity API。把数据拷贝出来或发事件到主线程。
    }

    void OnDestroy() {
        XRSDK.xr_pipeline_stop(m_pipeline);
        XRSDK.xr_pipeline_destroy(m_pipeline);
    }
}
#endif

// =============================================================================
// 使用示例 3：Android JNI（原生 App 调用）
// =============================================================================
#if 0
// --- Java 侧 ---
public class XRSDK {
    static { System.loadLibrary("XRSDK"); }

    public native long nativeCreate(String jsonConfig);
    public native int  nativePrepare(long handle);
    public native void nativeStart(long handle);
    public native void nativeStop(long handle);
    public native void nativeDestroy(long handle);
}

// --- JNI 侧 (xr_jni.cpp) ---
extern "C" JNIEXPORT jlong JNICALL
Java_com_ar_glasses_sdk_XRSDK_nativeCreate(JNIEnv* env, jobject thiz, jstring jsonConfig) {
    const char* json = env->GetStringUTFChars(jsonConfig, nullptr);
    auto* pipeline = new Pipeline(parseConfig(json)); // 包装成一个 C++ 对象
    env->ReleaseStringUTFChars(jsonConfig, json);
    return reinterpret_cast<jlong>(pipeline);
}
#endif

// =============================================================================
// 平台实现示例：Android VideoCapture（PAL 子类之一）
// =============================================================================
#if 0
#include <camera/NdkCameraManager.h>
#include <media/NdkImageReader.h>

class AndroidVideoCapture : public IVideoCapture {
public:
    bool open(int width, int height, int fps) override {
        // ① Camera2 API (NDK): ACameraManager_openCamera
        // ② 创建 ImageReader (YUV_420_888)
        // ③ 创建 CaptureSession，绑定 ImageReader 的 Surface
        // ④ 设置 RepeatingRequest
        return true;
    }

    void setFrameCallback(std::function<void(const VideoFrame&)> cb) override {
        m_onFrame = cb;
        // ImageReader.OnImageAvailableListener:
        //   AImage* img;
        //   VideoFrame f = { .data[0] = img->planes[0], .pts_us = img->timestamp, ... };
        //   m_onFrame(f);
        //   AImage_delete(img);   // ★ 归还 buffer pool
    }

    void start() override  { /* ACaptureSessionStart */ }
    void stop() override   { /* ACaptureSessionStop  */ }
    void close() override  { /* 释放 camera、session、reader */ }

private:
    std::function<void(const VideoFrame&)> m_onFrame;
    ACameraDevice* m_camera = nullptr;
    // ...
};
#endif
