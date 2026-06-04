# C 与 C++ 互操作及 FFmpeg/SDL 资源封装

> 高级音视频岗实战核心。FFmpeg、SDL、系统调用全是 **C 接口**，C++ 代码要天天和它们打交道。本篇考察的不是「会不会调」，而是**跨语言边界的正确姿势**：符号修饰、ABI 兼容、回调穿越、资源所有权、错误码到异常的转换。
>
> 前置：用 `unique_ptr` + 自定义 deleter 包 FFmpeg 资源见 [[06-智能指针与资源管理]]（本篇不重复，只深化）；符号与链接见 [[18-编译链接与构建]]；异常边界见 [[16-异常与异常安全]]；移动语义见 [[07-移动语义与右值引用]]、[[15-拷贝消除与三五零法则]]。

---

## 📌 面试速记（考前 5 分钟扫一遍）

- **`extern "C"` 的本质**：关闭 **name mangling（名字修饰）**，让 C++ 编译器按 C 的规则生成符号名，这样才能和 C 库的 `.o`/`.a`/`.so` 正确链接。
- **为什么 C 库头要包在 `extern "C" { ... }` 里**：C 库头若没自带 `#ifdef __cplusplus` 守卫，C++ 这边声明的函数符号会被修饰，链接时找不到 C 库里未修饰的符号 → **undefined reference**。
- **跨 C 边界只能传 POD/裸指针**：不能跨边界传 `std::string`/`std::vector`、不能让异常穿过 C 栈帧（UB），不能依赖 C++ ABI。
- **回调穿越 C 边界的唯一正确姿势**：传**静态函数 / 无捕获 lambda**（可转函数指针），把 `this` 通过 `void* userData` 带进去，回调里 `static_cast` 还原——FFmpeg/SDL 回调全是这套。
- **回调里必须吞掉异常**：异常穿过 C 写的栈帧是未定义行为，边界处 `try { ... } catch(...) {}` 兜底，转成错误码返回。
- **C 资源用 RAII 包**：`unique_ptr`+deleter（独占）、`shared_ptr`+deleter（共享一帧）、或封装 RAII 类（禁拷贝 + 实现移动 + `av_frame_ref` 做引用语义）。
- **错误码边界策略**：FFmpeg 返回**负错误码**（`AVERROR`），热路径（逐帧收发包）保持错误码，初始化等冷路径再转 `expected`/异常。
- **谁分配谁释放**：`av_malloc` 必须 `av_free`，C 库 alloc 的内存**绝不能用 `delete`**，不同分配器混用 = 堆损坏。

---

## 一、`extern "C"` 与 name mangling（符号修饰）

### 1. 问题根源：C++ 会修饰符号名，C 不会

C++ 支持**函数重载**——同名函数靠参数列表区分。但链接器只认符号名字符串，没有「参数」概念。于是 C++ 编译器把参数类型编码进符号名，这叫 **name mangling（名字修饰）**。C 没有重载，符号名就是函数名本身、不修饰。

```cpp
// C++ 源码
int decode(int frameCount);
int decode(double timestamp);
```

用 `nm`/`c++filt` 看修饰后的符号（GCC/Clang Itanium ABI）：

```text
# C++ 编译产物（修饰过，编码了参数类型）
_Z6decodei     ->  decode(int)
_Z6decoded     ->  decode(double)

# 同一个函数若按 C 规则编译，符号就是
decode
```

`_Z6decodei`：`_Z` 是 C++ 修饰前缀，`6decode` 是长度+函数名，`i` 是 `int` 参数；`d` 则是 `double`。不同编译器 mangling 方案不同（MSVC 与 Itanium 完全不同），这也是 **C++ 没有稳定跨编译器 ABI** 的原因之一。

### 2. `extern "C"` 做了什么

`extern "C"` 告诉 C++ 编译器：**这些声明按 C 的规则生成/查找符号——不修饰、用 C 调用约定**。

```cpp
extern "C" int decode(int frameCount); // 符号就是 "decode"，不再是 "_Z6decodei"
```

调用任何 C 库（FFmpeg、SDL、libcurl、系统库）时，C++ 代码必须用 `extern "C"` 声明那些函数，否则编译器按 C++ 规则去找 `_Z...` 符号，而 C 库的 `.so`/`.a` 里只有未修饰的符号——链接期报 **undefined reference**（这一步发生在链接而非编译，详见 [[18-编译链接与构建]]）。

```text
# 典型报错：声明被 mangling，链到 C 库时找不到修饰后的符号
undefined reference to `decode(int)'
```

### 3. C 库头里的 `#ifdef __cplusplus` 守卫

成熟的 C 库（含 FFmpeg、SDL）头文件自带这段守卫，所以**你直接 `#include` 它就行，不用手动包 `extern "C"`**：

```c
// 某个 C 库头 mylib.h 的标准写法
#ifdef __cplusplus
extern "C" {
#endif

int  mylib_open(const char* url);
void mylib_close(int handle);

#ifdef __cplusplus
}
#endif
```

`__cplusplus` 是 C++ 编译器预定义的宏，C 编译器没有。所以：C 编译器看到的是纯声明；C++ 编译器看到的是被 `extern "C" { }` 包住的声明。**一份头文件同时服务 C 和 C++**。

### 4. 什么时候要自己手动包 `extern "C"`

只有当某个老旧 C 库头**没写守卫**时，才需要在 C++ 这边手动包裹（[[06-智能指针与资源管理]] 里包 FFmpeg 头就是演示这种写法，实际新版 FFmpeg 头已自带守卫）：

```cpp
extern "C" {
    #include <legacy_c_lib.h>   // 这个头没有 __cplusplus 守卫
}
```

> 面试标准回答：「C++ 为了支持重载会做 name mangling，把参数类型编码进符号名；C 不修饰。`extern "C"` 让 C++ 按 C 规则生成符号，这样才能链接到 C 库。C 库头一般用 `#ifdef __cplusplus / extern "C" { } / #endif` 守卫，同一份头兼容 C 和 C++。如果声明没按 C 处理，链接时会因为符号名对不上报 undefined reference。」

---

## 二、调用约定与 ABI：跨 C 边界只能传什么

`extern "C"` 解决的是**符号名**问题，但「能链接上」不等于「能正确运行」。跨语言边界还要保证 **ABI（Application Binary Interface，二进制接口）兼容**：参数怎么压栈/进寄存器、谁清栈、对象内存布局如何。

### 1. 跨 C 边界只能用 C 兼容类型

暴露给 C 调用、或要穿过 C 边界的接口，参数和返回值**只能用 C 能理解的类型**：

- ✅ POD（基本类型 `int`/`double`、裸指针 `T*`、C 风格 struct、枚举、函数指针）。
- ❌ `std::string`、`std::vector`、`std::shared_ptr` 等——它们的内存布局是 C++ 实现细节，C 侧无法解析，甚至同一编译器不同版本布局都可能变。
- ❌ 不能让 C++ **异常**穿过 C 栈帧（下一节细讲）。
- ❌ 不能跨边界依赖模板、引用（`T&`，C 没有引用，要退化成指针）、默认参数、重载。

```cpp
// ❌ 反例：导出给 C 的接口里出现 C++ 类型
extern "C" std::string get_codec_name(int id);   // C 无法接收 std::string

// ✅ 正例：用 C 兼容签名，由调用方提供缓冲区
extern "C" int get_codec_name(int id, char* outBuffer, int bufferSize);
```

### 2. 把 C++ 库导出给 C 用：pimpl + `extern "C"` 包装

反过来，如果你写了个 C++ 类，想让 C 代码（或保证 ABI 稳定的插件边界）调用，标准模式是 **不透明指针（opaque pointer）+ extern "C" 自由函数**，把 C++ 对象藏在 `void*`/前向声明指针后面：

```cpp
// ---------- decoder.h：纯 C 可见的头 ----------
#ifdef __cplusplus
extern "C" {
#endif

typedef struct Decoder Decoder;        // 不透明类型，C 只见到指针

Decoder* decoder_create(const char* url);
int      decoder_decode_frame(Decoder* decoder);  // 返回错误码，0 成功
void     decoder_destroy(Decoder* decoder);

#ifdef __cplusplus
}
#endif
```

```cpp
// ---------- decoder.cpp：内部是地道的 C++ ----------
#include "decoder.h"
#include <string>
#include <memory>

struct Decoder {                       // 真正的 C++ 实现，藏在 .cpp 里
    std::string url;
    std::unique_ptr<MediaPipeline> pipeline;  // 想用什么 C++ 设施都行
};

extern "C" Decoder* decoder_create(const char* url) {
    try {
        auto* decoder = new Decoder{url, std::make_unique<MediaPipeline>(url)};
        return decoder;                // 以裸指针形式交给 C
    } catch (...) {
        return nullptr;                // 异常绝不外泄到 C，转成 nullptr
    }
}

extern "C" int decoder_decode_frame(Decoder* decoder) {
    try {
        decoder->pipeline->decodeOneFrame();
        return 0;
    } catch (const std::exception&) {
        return -1;                     // 异常 → 错误码
    }
}

extern "C" void decoder_destroy(Decoder* decoder) {
    delete decoder;                    // new 出来的，必须 delete，且在本侧释放
}
```

要点：所有 `extern "C"` 入口都用 `try/catch(...)` 兜底，C++ 内部细节（`std::string`、`unique_ptr`）完全不出现在头文件里。这也是「**谁分配谁释放**」——对象在 C++ 侧 `new`，就必须在 C++ 侧 `delete`，C 侧只持有 `Decoder*` 句柄。

> 面试标准回答：「跨 C 边界只能传 POD 和裸指针，不能传 STL 容器或让异常穿过去，因为那些是 C++ ABI 细节，C 解析不了。要把 C++ 库给 C 用，就用 pimpl/不透明指针：头文件里只暴露一个前向声明的指针类型和一组 `extern "C"` 自由函数，实现藏在 cpp 里，入口处用 try-catch 把异常转成错误码。」

---

## 三、回调函数跨 C 边界（音视频高频重点）

C API 的回调是**函数指针**，签名形如 `void(*)(void* userData, ...)`。FFmpeg 的自定义 IO、日志回调，SDL 的音频回调全是这套。难点在于：**C++ 的成员函数和带捕获的 lambda 都不能直接转成 C 函数指针**。

### 1. 为什么不能直接传成员函数 / 带捕获 lambda

- **非静态成员函数**有隐藏的 `this` 参数，类型是 `void (Class::*)(...)`，和 `void(*)(...)` 二进制布局不同，无法转换。
- **带捕获的 lambda** 是一个有状态的匿名类对象，编译器要为它存捕获的变量，所以它**没有**到普通函数指针的转换。
- **无捕获的 lambda** 没有状态，C++ 标准保证它能隐式转换成普通函数指针——**这个可以传**。

```cpp
struct Player {
    int volume = 0;
    void onAudio(unsigned char* stream, int len);   // 隐藏 this，类型不匹配
};

// ❌ 全部编译错误
SDL_AudioSpec spec;
spec.callback = &Player::onAudio;                    // 成员函数指针，类型不符
spec.callback = [volume](unsigned char* s, int l){}; // 带捕获，无法转函数指针
```

### 2. 正确模式：静态跳板 + `void* userData` 传 `this`

唯一可移植的姿势：用**静态成员函数 / 无捕获 lambda** 作为「跳板（trampoline）」满足函数指针类型，把 `this` 通过 C API 预留的 `void* userData` 透传进去，回调里 `static_cast` 还原（`void*` 到具体类型的转换见 [[11-类型转换]]）。

### 3. 示例 A：FFmpeg `av_log_set_callback`（日志回调）

```cpp
extern "C" {
    #include <libavutil/log.h>
}
#include <cstdarg>
#include <cstdio>

// 回调签名由 FFmpeg 规定：void(*)(void*, int, const char*, va_list)
// 写成无捕获 lambda 即可转成函数指针；这里不需要 userData
static void logCallback(void* avcl, int level, const char* fmt, va_list args) {
    if (level > av_log_get_level()) {
        return;
    }
    char lineBuffer[1024];
    vsnprintf(lineBuffer, sizeof(lineBuffer), fmt, args);
    // 转发到自己的日志系统；注意：绝不让异常在这里抛出去
    fprintf(stderr, "[ffmpeg] %s", lineBuffer);
}

void installFfmpegLog() {
    av_log_set_level(AV_LOG_WARNING);
    av_log_set_callback(logCallback);   // 函数指针，OK
}
```

### 4. 示例 B：SDL 音频回调把 `this` 经 `userData` 带回来

SDL 在独立的音频线程里回调 `callback(void* userdata, Uint8* stream, int len)`，要它驱动一个 C++ 播放器对象，就把 `this` 塞进 `userdata`：

```cpp
extern "C" {
    #include <SDL2/SDL.h>
}

class AudioPlayer {
public:
    void open() {
        SDL_AudioSpec wanted{};
        wanted.freq     = 44100;
        wanted.format   = AUDIO_S16SYS;
        wanted.channels = 2;
        wanted.samples  = 1024;
        wanted.callback = &AudioPlayer::sdlAudioTrampoline; // 静态成员，类型匹配
        wanted.userdata = this;                             // 把 this 透传给 C
        SDL_OpenAudio(&wanted, nullptr);
        SDL_PauseAudio(0);
    }

private:
    // 静态跳板：类型是纯 C 函数指针，没有隐藏 this
    static void sdlAudioTrampoline(void* userData, Uint8* stream, int len) {
        auto* self = static_cast<AudioPlayer*>(userData); // 还原回 this
        self->fillAudio(stream, len);                     // 转调真正的成员函数
    }

    // 真正的业务逻辑，普通成员函数，可自由用 C++ 设施
    void fillAudio(Uint8* stream, int len) {
        // ……从队列取 PCM 填到 stream……
    }
};
```

> 面试标准回答：「C 回调是函数指针，成员函数带隐藏 this、带捕获 lambda 有状态，都不能转函数指针。正确做法是用静态成员函数或无捕获 lambda 当跳板满足函数指针类型，把 this 通过 C API 的 `void* userdata` 传进去，回调里 `static_cast` 还原再转调成员函数。SDL 音频、FFmpeg 自定义 IO 都是这个套路。」

### 5. 示例 C：FFmpeg 自定义 IO `avio_alloc_context`（read_packet 回调）

让 FFmpeg 从你自己的数据源（内存、网络、加密流）读取数据，要提供 `read_packet` 回调。同样靠 `opaque`（即 userData）把 C++ 对象带进去：

```cpp
extern "C" {
    #include <libavformat/avformat.h>
    #include <libavformat/avio.h>
}

class MemoryStream {
public:
    AVFormatContext* openAsInput() {
        constexpr int bufferSize = 32 * 1024;
        // FFmpeg 要求用 av_malloc 分配这个缓冲（内部会用 av_free 释放）
        unsigned char* ioBuffer = static_cast<unsigned char*>(av_malloc(bufferSize));

        // read_packet 用无捕获 lambda 转函数指针；opaque 透传 this
        ioContext_ = avio_alloc_context(
            ioBuffer, bufferSize,
            /*write_flag=*/0,
            /*opaque=*/this,                 // 把 this 交给 C
            &MemoryStream::readPacket,       // 静态跳板
            nullptr, &MemoryStream::seek);

        formatContext_ = avformat_alloc_context();
        formatContext_->pb = ioContext_;
        avformat_open_input(&formatContext_, nullptr, nullptr, nullptr);
        return formatContext_;
    }

private:
    // 签名由 FFmpeg 规定：int(*)(void* opaque, uint8_t* buf, int bufSize)
    static int readPacket(void* opaque, uint8_t* buf, int bufSize) {
        auto* self = static_cast<MemoryStream*>(opaque);
        return self->readImpl(buf, bufSize); // 返回读到的字节数，或 AVERROR_EOF
    }
    static int64_t seek(void* opaque, int64_t offset, int whence) {
        return static_cast<MemoryStream*>(opaque)->seekImpl(offset, whence);
    }

    int readImpl(uint8_t* buf, int bufSize) {
        if (position_ >= data_.size()) {
            return AVERROR_EOF;          // 用 FFmpeg 的错误码表达 EOF，不要抛异常
        }
        int n = std::min<int>(bufSize, static_cast<int>(data_.size() - position_));
        std::memcpy(buf, data_.data() + position_, n);
        position_ += n;
        return n;
    }
    int64_t seekImpl(int64_t offset, int whence) { /* ... */ return 0; }

    std::vector<uint8_t> data_;
    size_t position_ = 0;
    AVIOContext* ioContext_ = nullptr;
    AVFormatContext* formatContext_ = nullptr;
};
```

注意 `avio_alloc_context` 的缓冲必须用 **`av_malloc`** 分配——FFmpeg 内部按自己的对齐规则用它，释放也走 `av_free`（所有权细节见第六节）。

### 6. 回调里抛异常穿过 C 栈帧 = 未定义行为

这是边界处最致命的坑。FFmpeg/SDL 是 C 编译的，调用栈里**没有异常处理的栈展开信息**。如果你的回调（运行在 C 调用的栈帧上）抛出 C++ 异常，异常要往上传播、却要穿过 C 的栈帧——这是**未定义行为**，轻则 `std::terminate`，重则栈损坏崩溃（异常安全与栈展开见 [[16-异常与异常安全]]）。

**铁律：C 边界（回调入口、`extern "C"` 入口）必须吞掉所有异常，转成错误码。**

```cpp
static int readPacket(void* opaque, uint8_t* buf, int bufSize) {
    try {
        return static_cast<MemoryStream*>(opaque)->readImpl(buf, bufSize);
    } catch (...) {
        // 绝不让异常逃出去穿过 FFmpeg 的 C 栈帧
        return AVERROR_EXTERNAL;          // 用错误码告诉 FFmpeg 出错了
    }
}
```

> 面试标准回答：「回调跑在 C 库的栈帧上，C 代码没有 C++ 的栈展开信息，异常一旦穿过 C 栈帧就是未定义行为，可能直接 terminate 或栈损坏。所以所有 C 边界——回调和 `extern "C"` 导出函数——都要 `try{...}catch(...)` 兜底，把异常转成库约定的错误码（比如 FFmpeg 的 `AVERROR_EXTERNAL`）。」

---

## 四、用 RAII / 智能指针封装 FFmpeg 资源

### 1. 基线：`unique_ptr` + 自定义 deleter（独占）

最常用的写法——给 `unique_ptr` 配一个调用 `av_*_free` 的 deleter，独占管理一个 `AVFrame`/`AVPacket`。**这部分 [[06-智能指针与资源管理]] 已详讲**（含二级指针、`get/release/reset` 在 C API 的用法），这里一句话带过：

```cpp
struct AVFrameDeleter { void operator()(AVFrame* f) const { av_frame_free(&f); } };
using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;
AVFramePtr frame(av_frame_alloc());   // 独占，离开作用域自动 av_frame_free
```

下面深化 06 没覆盖的三点：**shared 共享、RAII 类带引用语义、成对资源封装**。

### 2. `shared_ptr` 管 C 资源：多处共享同一帧

解码后的一帧可能被多个消费者（渲染、编码、缩略图）同时引用，生命周期由「最后一个用完的人」决定——这时用 `shared_ptr` + deleter：

```cpp
// shared_ptr 的 deleter 写成构造参数即可（不像 unique_ptr 进类型）
std::shared_ptr<AVFrame> sharedFrame(av_frame_alloc(),
                                     [](AVFrame* f){ av_frame_free(&f); });

// 任意拷贝，引用计数管理；最后一个析构时调用 av_frame_free
std::shared_ptr<AVFrame> consumerCopy = sharedFrame;  // 计数 +1
```

**何时用 unique，何时用 shared**：

| 场景 | 选择 |
| :--- | :--- |
| 一个 frame 只在一条流水线里顺序流转，单一所有者 | `unique_ptr` + deleter（零开销） |
| 一帧被多个线程/模块同时持有，谁都可能最后释放 | `shared_ptr` + deleter（原子计数） |
| 只是临时借用、不管生命周期 | 裸 `AVFrame*`（不拥有，别 free） |

> 默认优先 `unique_ptr`；只有确实存在「共享所有权、释放时机不确定」时才上 `shared_ptr`，它有控制块和原子计数的开销。

### 3. 封装成 RAII 类（带移动语义 + 引用语义）

更工程化的做法是封装成一个 RAII 类：**构造时 alloc、析构时 free、禁拷贝、实现移动**。AVFrame 本身是「引用计数的帧」——它内部的像素 buffer 由 `av_frame_ref`/`av_frame_unref` 共享。所以拷贝语义要用 `av_frame_ref` 表达「共享底层 buffer」，移动则是「转移所有权」（移动语义见 [[07-移动语义与右值引用]]，禁拷贝/移动的取舍见 [[15-拷贝消除与三五零法则]]）。

```cpp
extern "C" {
    #include <libavutil/frame.h>
}
#include <utility>
#include <stdexcept>

class FrameRAII {
public:
    FrameRAII() : frame_(av_frame_alloc()) {
        if (!frame_) {
            throw std::bad_alloc();          // 构造失败抛异常（这是冷路径，可接受）
        }
    }
    ~FrameRAII() {
        av_frame_free(&frame_);              // free 接受 nullptr，安全
    }

    // 禁拷贝：默认拷贝会复制裸指针 → 两次 free 同一对象（double free）
    FrameRAII(const FrameRAII&)            = delete;
    FrameRAII& operator=(const FrameRAII&) = delete;

    // 移动构造：转移所有权，把源置空（noexcept，配合容器扩容用移动）
    FrameRAII(FrameRAII&& other) noexcept : frame_(other.frame_) {
        other.frame_ = nullptr;
    }
    // 移动赋值：先释放自己原有的，再接管
    FrameRAII& operator=(FrameRAII&& other) noexcept {
        if (this != &other) {
            av_frame_free(&frame_);
            frame_ = other.frame_;
            other.frame_ = nullptr;
        }
        return *this;
    }

    // 显式的「引用」语义：共享底层 buffer（引用计数 +1），而非深拷贝像素
    FrameRAII ref() const {
        FrameRAII copy;                      // 新建一个空壳
        av_frame_unref(copy.frame_);         // 清掉 alloc 时的初始状态
        if (av_frame_ref(copy.frame_, frame_) < 0) {
            throw std::runtime_error("av_frame_ref failed");
        }
        return copy;                         // RVO / 移动返回
    }

    AVFrame*       get()       { return frame_; }
    const AVFrame* get() const { return frame_; }

private:
    AVFrame* frame_ = nullptr;
};
```

要点：禁拷贝避免 double free；移动 `noexcept` 让它能高效放进 `std::vector`；`ref()` 用 `av_frame_ref` 表达「共享同一块像素 buffer」，这是 AVFrame 的正确「拷贝」方式，比 memcpy 整帧像素便宜得多。

### 4. 成对资源：分配 + open，析构对称释放

FFmpeg 很多资源是「**两步获取、对应两步释放**」的，封装时析构要严格对称：

| 资源 | 获取 | 释放 |
| :--- | :--- | :--- |
| `AVFormatContext`（输入） | `avformat_open_input(&ctx, ...)` | `avformat_close_input(&ctx)` |
| `AVFormatContext`（仅 alloc） | `avformat_alloc_context()` | `avformat_free_context(ctx)` |
| `AVCodecContext` | `avcodec_alloc_context3()` + `avcodec_open2()` | `avcodec_free_context(&ctx)` |

**坑**：`AVFormatContext` 用 `avformat_open_input` 打开的，必须用 `avformat_close_input` 关（它会顺带释放结构体）；只 `alloc` 没 open 成功的，才用 `avformat_free_context`。混用会泄漏或重复释放。

```cpp
extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
}

// 解封装上下文：open 成功后 close_input 一次性回收
class DemuxerRAII {
public:
    explicit DemuxerRAII(const char* url) {
        // open_input 接收二级指针：失败时它会自己释放并把指针置空
        int errorCode = avformat_open_input(&formatContext_, url, nullptr, nullptr);
        if (errorCode < 0) {
            throw std::runtime_error("avformat_open_input failed");
        }
    }
    ~DemuxerRAII() {
        if (formatContext_) {
            avformat_close_input(&formatContext_); // 注意：不是 free_context
        }
    }
    DemuxerRAII(const DemuxerRAII&)            = delete;
    DemuxerRAII& operator=(const DemuxerRAII&) = delete;
    DemuxerRAII(DemuxerRAII&& other) noexcept : formatContext_(other.formatContext_) {
        other.formatContext_ = nullptr;
    }

    AVFormatContext* get() const { return formatContext_; }

private:
    AVFormatContext* formatContext_ = nullptr;
};

// 解码器上下文：alloc + open 两步，析构用 avcodec_free_context
class DecoderContextRAII {
public:
    DecoderContextRAII(const AVCodec* codec, const AVCodecParameters* params) {
        codecContext_ = avcodec_alloc_context3(codec);
        if (!codecContext_) {
            throw std::bad_alloc();
        }
        // 用 RAII 管住 codecContext_ 后再做可能失败的步骤，保证不泄漏
        if (avcodec_parameters_to_context(codecContext_, params) < 0 ||
            avcodec_open2(codecContext_, codec, nullptr) < 0) {
            avcodec_free_context(&codecContext_); // 失败也要回收已 alloc 的
            throw std::runtime_error("open decoder failed");
        }
    }
    ~DecoderContextRAII() { avcodec_free_context(&codecContext_); }

    DecoderContextRAII(const DecoderContextRAII&)            = delete;
    DecoderContextRAII& operator=(const DecoderContextRAII&) = delete;

    AVCodecContext* get() const { return codecContext_; }

private:
    AVCodecContext* codecContext_ = nullptr;
};
```

> 面试标准回答：「FFmpeg 资源我一律用 RAII 封装：构造 alloc、析构 free，禁拷贝防 double free，需要转移就实现 noexcept 移动。AVFrame 这种引用计数对象，'拷贝' 要用 `av_frame_ref` 共享底层 buffer。成对资源要对称释放——`avformat_open_input` 配 `avformat_close_input`，`avcodec_alloc_context3`+`avcodec_open2` 配 `avcodec_free_context`，且构造中途失败也要回收已分配的部分。」

---

## 五、错误码处理：C 的负错误码到 C++ 的转换

### 1. FFmpeg 的错误码约定

C API 没有异常，靠**返回值传错误**。FFmpeg 约定：**返回 0 或正数表示成功/字节数，负数表示错误**。错误码用 `AVERROR` 系列宏表达：

- `AVERROR(EAGAIN)`：暂时没数据，**不是真错误**——`avcodec_send_packet`/`avcodec_receive_frame` 收发包循环里要靠它判断「现在没法继续、再喂/再取」。
- `AVERROR_EOF`：流结束。
- `AVERROR(ENOMEM)`、`AVERROR(EINVAL)` 等：包裹 errno 的真错误。
- `av_strerror(errorCode, buf, size)`：把错误码翻成可读字符串。

```cpp
char errorBuffer[AV_ERROR_MAX_STRING_SIZE]{};
av_strerror(errorCode, errorBuffer, sizeof(errorBuffer));
// 或用宏 av_make_error_string / av_err2str
```

### 2. 收发包热路径：保持错误码，别抛异常

逐帧解码是**热路径**，`EAGAIN`/`EOF` 是高频且可预期的正常控制流，绝不该用异常表达（异常 vs 错误码的权衡见 [[16-异常与异常安全]]）：

```cpp
int decodePacket(AVCodecContext* codecContext, AVPacket* packet, FrameSink& sink) {
    int errorCode = avcodec_send_packet(codecContext, packet);
    if (errorCode < 0) {
        return errorCode;                       // 真错误，原样上抛错误码
    }
    while (errorCode >= 0) {
        FrameRAII frame;
        errorCode = avcodec_receive_frame(codecContext, frame.get());
        if (errorCode == AVERROR(EAGAIN) || errorCode == AVERROR_EOF) {
            return 0;                           // 不是错误：本轮取完了，正常返回
        }
        if (errorCode < 0) {
            return errorCode;                   // 真错误
        }
        sink.consume(std::move(frame));         // 移动出去，零拷贝交付
    }
    return 0;
}
```

### 3. 冷路径：包装成 C++ 风格（`expected` / `optional` / 异常）

初始化、打开文件等**冷路径**（一次性、失败即终止）可以把错误码转成 C++ 风格的返回，让上层代码更干净。C++23 用 `std::expected`，更早可用 `std::optional` 或自定义结果类型：

```cpp
#include <expected>     // C++23
#include <string>

// 把 FFmpeg 错误码封成可读的失败信息
std::expected<DemuxerRAII, std::string> openDemuxer(const char* url) {
    AVFormatContext* formatContext = nullptr;
    int errorCode = avformat_open_input(&formatContext, url, nullptr, nullptr);
    if (errorCode < 0) {
        char buf[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(errorCode, buf, sizeof(buf));
        return std::unexpected(std::string("open failed: ") + buf);
    }
    return DemuxerRAII::adopt(formatContext);   // 用工厂接管已打开的句柄
}

// 调用方：分支清晰，没有裸错误码到处传
auto result = openDemuxer("input.mp4");
if (!result) {
    log(result.error());
    return;
}
auto& demuxer = *result;
```

**边界策略总结**：热路径（逐帧收发）保留原生错误码，零开销且能精确区分 `EAGAIN`/`EOF`；冷路径（初始化）转成 `expected`/异常，换取调用方代码整洁。这条线和 [[16-异常与异常安全]] 里「异常留给真正异常的路径」是一致的。

> 面试标准回答：「FFmpeg 用返回值传错误，负数是错误码，`AVERROR(EAGAIN)` 和 `AVERROR_EOF` 是收发包循环的正常控制信号不是错误。我的策略是分层：解码热路径保留原生错误码，避免每帧抛异常的开销，也方便区分 EAGAIN/EOF；初始化这种冷路径再用 `av_strerror` 转成可读信息，包成 `std::expected` 或异常给上层。」

---

## 六、内存所有权跨边界

跨 C/C++ 边界，最易出隐蔽 bug 的就是**谁负责释放、用哪个释放器**。

### 1. 谁分配谁释放 + 配对的分配/释放器

核心原则：**哪个模块/分配器分配的，就用对应的释放器在对应侧释放**。不同分配器的内存绝不能混用：

| 分配 | 释放 | 混用后果 |
| :--- | :--- | :--- |
| `av_malloc` / `av_mallocz` | `av_free` / `av_freep` | 用 `free`/`delete` → 堆损坏（FFmpeg 有自己的对齐策略） |
| `malloc`（C） | `free`（C） | 用 `delete` → UB |
| `new` / `new[]`（C++） | `delete` / `delete[]` | 用 `free` → 不调析构 + 堆损坏 |
| `av_frame_alloc` | `av_frame_free` | 用 `free` → 内部子 buffer 泄漏 |

```cpp
// ❌ 致命：C 库分配的内存用 C++ 的 delete
uint8_t* buf = static_cast<uint8_t*>(av_malloc(1024));
delete[] buf;                 // UB：av_malloc 和 delete 不是一套分配器

// ✅ 正确：成对使用
uint8_t* buf = static_cast<uint8_t*>(av_malloc(1024));
av_freep(&buf);               // av_freep 释放后顺带把指针置 nullptr
```

为什么不能混：`av_malloc` 会做 SIMD 友好的对齐（通常 32/64 字节），可能在返回指针前多分配了对齐填充和元信息，`delete`/`free` 看到的不是它认识的堆块头，于是破坏堆结构（对齐与 SIMD 见 [[21-内存对齐、SIMD与缓冲管理]]）。

### 2. 传入参数 vs 传出参数：看清所有权是否转移

读 C API 文档第一件事是分清参数的所有权语义：

- **传入只读（borrow）**：`const T*`，C 库只读不接管，你仍负责释放。
- **传出新分配（transfer out）**：`T**`，库分配后写给你，**你负责释放**（如 `avformat_open_input` 写出的 `AVFormatContext*`，你要 `avformat_close_input`）。
- **传入接管（transfer in）**：你把指针交给库，库负责后续释放（如 `avio_alloc_context` 的缓冲，理论上由你 `av_free`，但若挂到 `AVFormatContext` 上则随之回收——以文档为准）。

```cpp
AVDictionary* options = nullptr;
av_dict_set(&options, "rtsp_transport", "tcp", 0); // 库帮你分配/扩容字典
// ……用完……
av_dict_free(&options);                            // 必须由你释放，否则泄漏
```

### 3. 用 RAII 兜住「传出参数」的释放

传出参数最容易在中途 `return`/异常时漏释放，正确做法是拿到后立刻交给 RAII（前面的 `DemuxerRAII`/deleter 就是干这个），让作用域退出时自动走对应释放器，而不是手写 `goto fail` 式清理。

> 面试标准回答：「跨边界第一原则是谁分配谁释放、分配器要配对：`av_malloc` 配 `av_free`，绝不能用 `delete` 去释放 C 库的内存，因为 FFmpeg 有自己的对齐和堆块布局，混用会堆损坏。其次要分清参数是借用、传出新分配还是接管——传出的（二级指针写回来的）由我释放，我一般拿到手立刻塞进 RAII 包装，避免中途 return 或异常漏掉释放。」

---

## 七、音视频实战场景小结

把前面的点串成一个典型播放器里 C/C++ 互操作的全景：

- **解封装/解码层**：`AVFormatContext`、`AVCodecContext`、`AVFrame`、`AVPacket` 全用 RAII 类或 `unique_ptr`+deleter 管，禁拷贝、实现移动；逐帧 `send/receive` 用错误码循环处理 `EAGAIN`/`EOF`。
- **自定义数据源**：从内存/网络/解密流喂数据用 `avio_alloc_context` + `read_packet` 回调，`opaque` 透传 `this`，回调 `try/catch(...)` 兜底，缓冲用 `av_malloc`。
- **渲染/音频输出层**：SDL 音频回调跑在独立线程，用静态跳板 + `userdata` 拿到播放器对象；回调里只做填数据，绝不抛异常；PCM 队列跨线程要加锁（见 [[01-多线程与锁]]）。
- **一帧多消费者**：解码出的 `AVFrame` 要同时给渲染和录制时，用 `shared_ptr<AVFrame>`+deleter，或 `av_frame_ref` 共享底层 buffer，避免整帧像素深拷贝。
- **日志/诊断**：`av_log_set_callback` 接管 FFmpeg 日志转发到自家日志系统，回调用无捕获 lambda/静态函数。
- **对外 SDK**：若把播放器封成 SDK 给别的语言/模块用，走 pimpl + `extern "C"`，句柄用不透明指针，所有入口 `try/catch(...)` 转错误码。

---

## 常见坑

| 坑 | 后果 | 正确做法 |
| :--- | :--- | :--- |
| C 库头没包 `extern "C"` 就声明其函数 | 链接期 undefined reference | 用自带 `__cplusplus` 守卫的头，或手动包 `extern "C" { }` |
| 跨 C 边界传 `std::string`/`vector`/引用 | ABI 不兼容、无法编译或 UB | 只传 POD、裸指针、C struct |
| 把成员函数 / 带捕获 lambda 当 C 回调 | 编译错误（类型不匹配） | 静态函数/无捕获 lambda 跳板 + `void* userData` 传 `this` |
| 回调里让异常抛出穿过 C 栈帧 | UB / terminate / 栈损坏 | 边界 `try{...}catch(...)` 吞掉转错误码 |
| `av_malloc` 的内存用 `delete`/`free` | 堆损坏 | 配对用 `av_free`/`av_freep` |
| RAII 类没禁拷贝 | 浅拷贝裸指针 → double free | 禁拷贝 + 实现 `noexcept` 移动 |
| 移动构造忘了置空源指针 | double free | 移动后把源指针设 `nullptr` |
| `avformat_open_input` 用 `avformat_free_context` 释放 | 泄漏/重复释放 | open 的用 `avformat_close_input` |
| 把 `EAGAIN`/`EOF` 当成错误抛异常 | 热路径性能差、逻辑错乱 | 它们是正常控制流，用错误码分支处理 |
| 传出参数（二级指针）忘了释放 | 内存泄漏 | 拿到立即交给 RAII / deleter |
| `AVDictionary`/`AVFormatContext` 等漏配对释放器 | 泄漏 | 查文档确认所有权，配 `av_dict_free` 等 |

> 面试一句话总结：「和 FFmpeg/SDL 这类 C 库互操作，核心是守住边界：`extern "C"` 解决符号修饰让链接通过；跨边界只传 POD 和裸指针；回调用静态跳板 + `void*` 透传 `this`，且必须吞掉异常不让它穿过 C 栈帧；所有 C 资源用 RAII 封装、禁拷贝实现移动，分配器严格配对（`av_malloc`/`av_free`）、成对资源对称释放；错误处理分层——热路径保留 FFmpeg 负错误码，冷路径再转 `expected` 或异常。」









