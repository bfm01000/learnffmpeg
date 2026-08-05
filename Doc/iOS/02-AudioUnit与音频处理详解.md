# AudioUnit 与 iOS 音频处理：面试速记与原理详解

## 0. 本篇定位

- 面试复习：先掌握 RemoteIO、bus/scope、ASBD、音频回调线程和 `AVAudioSession` 的关系。
- 深入学习：重点看实时音频线程不能阻塞、不能频繁分配内存，以及 RTC 场景下回声消除和路由变化处理。
- 工程落点：iOS 音频质量靠线程纪律、buffer 尺寸、session 策略和时钟同步共同保证。
> **适用方向**：iOS 移动端音视频 SDK 开发（实时音频采集/播放方向）
> **前置知识**：PCM 音频基础（采样率、位深、声道），了解 Android AudioTrack/AudioRecord 最好
> **难度**：⭐⭐⭐（1-5 星）
> **预计阅读**：速记 10 分钟｜全文 25 分钟
> **关联文档**：[[00-iOS音视频开发全景导读]] · [[01-AVFoundation采集详解]] · [[../ffmpeg/00-FFmpeg全景导读]] §3.3 音频采样格式
> **定位**：🟡 高级加分 —— 纯视频场景可用高层 AVAudioEngine，RTC/直播/实时处理才必须下到 AudioUnit

---

## 一、全景导读：AudioUnit 在 iOS 音频栈的位置

### 1.1 从场景说起

你已经搞定了 iOS 端视频的采集和编解码。现在要做直播或视频通话——必须有声音。你打开 iOS 的音频文档，迎面是 AVAudioPlayer、AVAudioRecorder、AVAudioEngine、AudioQueue、AudioUnit……到底选哪个？

和你学 Android 音频时一样——**你要的是能实时拿到 PCM 数据自己处理的 API**。在 Android 上是 AudioRecord（录）和 AudioTrack（播），搭配 OpenSL ES 或 AAudio。在 iOS 上，答案就是 **AudioUnit**。

### 1.2 iOS 音频框架分层

```
高层（省心，拿不到 PCM）
  AVAudioPlayer（播文件）· AVAudioRecorder（录文件）
  AVAudioEngine（实时音频图，是对 AudioUnit 的 Swift 层封装）

中层（精细控制，逐 buffer 回调 PCM）
  AudioQueue（C API，适合播/录文件但有 buffer 队列控制）
  AudioUnit（C API，最底层但最灵活——播、录、实时处理都行）

底层（硬件交互）
  AudioSession（管理 App 的音频策略：中断、路由、混音、
                采样率——**不属于 AudioUnit 但对它做的事有全局影响**）
  I/O Kit Audio Driver（内核级，一般应用不碰）
```

### 1.3 AudioUnit 是什么

**一句话**：AudioUnit 是 iOS/macOS 原始音频处理模块，一个 AudioUnit 可以是一段 DSP 效果器（混响、均衡器）、一个混音器、一个格式转换器，或者——我们最关心的——**一个音频 I/O 设备（麦克风 + 扬声器）**。

**RemoteIO**（kAudioUnitSubType_RemoteIO）是 iOS 上最常用的 AudioUnit 子类型——**它一个 Unit 同时管输入（麦克风 → 你的 App）和输出（你的 App → 扬声器）**。

```
┌────────────────────────────────────────────────┐
│                AudioUnit (RemoteIO)              │
│                                                  │
│   输入总线 (Bus 1)         输出总线 (Bus 0)        │
│   麦克风 ──→ [input]      [output] ──→ 扬声器      │
│              │                         ↑          │
│              │   input callback        │          │
│              │   系统送 PCM 给你        │          │
│              ↓                         │          │
│         ┌──────────────────────────────┐          │
│         │  你的业务代码（C++ 音频引擎）   │          │
│         │  回声消除 / 降噪 / 编码 / 混音 │          │
│         └──────────────────────────────┘          │
│                         │                        │
│                         ↓                        │
│                    render callback               │
│                    你送 PCM 给系统               │
└────────────────────────────────────────────────┘
```

**和 Android 的对应**：
- AudioUnit RemoteIO ≈ AudioRecord（input callback）+ AudioTrack（render callback）合在一个对象里
- iOS 的 AudioSession ≈ Android 的 AudioManager + AudioAttributes（控制焦点、路由、中断）

### 1.4 学习优先级

| 层级 | 内容 | 重要度 |
|------|------|--------|
| 🟢 中级 | 理解 AudioUnit RemoteIO 的双向模型 | 🔥🔥🔥 |
| 🟢 中级 | 配置 ASBD（AudioStreamBasicDescription） | 🔥🔥🔥 |
| 🟢 中级 | 写 input callback 和 render callback | 🔥🔥🔥 |
| 🟡 高级 | AudioSession 管理（中断/路由/后台） | 🔥🔥 |
| 🟡 高级 | AUGraph（连接多个 AudioUnit） | 🔥🔥 |
| 🔵 专家 | Core Audio 底层（AudioConverter、ring buffer 实现） | 🔥 |

---

## 二、面试速记（考前 10 分钟扫一遍）

### 2.1 高频考点速查

| # | 考点 | 一句话答案 | 频率 | 难度 |
|---|------|-----------|------|------|
| 1 | iOS 怎么做实时音频采集 | AudioUnit RemoteIO 的 input callback | 🔥🔥🔥 | 🟢 |
| 2 | 怎么配置 PCM 格式 | ASBD 填采样率/位深/声道/交错或平面 | 🔥🔥🔥 | 🟢 |
| 3 | 回调在哪个线程 | 系统高优先级音频线程，不能阻塞/加锁/alloc | 🔥🔥🔥 | 🟢 |
| 4 | 为什么要用 ring buffer | 解耦音频线程和业务线程，避免音频线程阻塞 | 🔥🔥🔥 | 🟡 |
| 5 | AudioSession 干什么 | 管理音频策略：中断、路由、混音、采样率 | 🔥🔥 | 🟡 |
| 6 | 回声消除怎么做 | VoiceProcessingIO 替代 RemoteIO，系统内置 AEC | 🔥🔥 | 🟡 |
| 7 | 为什么不用 AVAudioEngine | 高层封装灵活性差，实时推流/RTC 需要精细控制 | 🔥 | 🟡 |

### 2.2 面试标准回答

#### Q1：iOS 端怎么做实时音频采集和播放？

**面试官想听什么：** 会不会用 AudioUnit RemoteIO，理不理解它的双向模型。

**🗣️ 标准回答（可背诵）：**

> "iOS 做实时音频用 AudioUnit 的 RemoteIO 子类型。它有两个总线——总线 1 是输入，接麦克风，通过 input callback 给你 PCM 数据；总线 0 是输出，接扬声器，通过 render callback 问你要 PCM 数据去播。两个 callback 都跑在系统的高优先级音频线程上，绝对不能阻塞。我的做法是在回调里做最简单的操作——input callback 把 PCM 写入一个无锁环形缓冲（ring buffer），render callback 从 ring buffer 里读数据——然后另外起一条业务线程做编码/降噪/推流这些耗时操作。配置上要设好 ASBD 流格式描述：LinearPCM、16 位整数、采样率 48kHz 或 44.1kHz、交错存储（interleaved），这个和 Android 的 AudioTrack/AudioRecord 的配置基本对应。"

**👨‍💻 追问预警：**
> Q: "音频线程上为什么不能加锁？"
> A: 因为音频线程有硬实时要求——以 48kHz 为例，一个 buffer 大概 5-10ms，处理超时直接导致音频卡顿或爆音。加锁可能触发优先级反转——如果另一个低优先级线程持有锁，音频线程被阻塞等不来 CPU，系统音频缓冲区就会被掏空。所以音频线程上只能用 lock-free 的数据结构——通常是一个 SPSC ring buffer。

---

#### Q2：ASBD 是什么？怎么配置？

**面试官想听什么：** 你能不能正确配置 PCM 格式，理解每个字段的含义。

**🗣️ 标准回答（可背诵）：**

> "ASBD 全称 AudioStreamBasicDescription，是 Core Audio 里描述音频流格式的结构体，相当于 FFmpeg 里的 AVPixelFormat + 采样率 + 声道布局的组合。配置实时音频采集/播放的标准填法是：mFormatID 设为 LinearPCM，mFormatFlags 设为 SignedInteger 加 Packed（表示有符号 16 位整数、交错排列），mBitsPerChannel 设 16，mChannelsPerFrame 单声道是 1 立体声是 2，mSampleRate 设 48000 或 44100，mFramesPerPacket 设 1（PCM 天然每 packet 就是 1 帧），mBytesPerFrame 是 (位深/8)×声道数，mBytesPerPacket 和 mBytesPerFrame 一样。这里要注意——如果设错了比如 mFormatFlags 设成了 Float 但 mBitsPerChannel 设成了 16，AudioUnit 初始化直接失败，而且错误信息非常不友好。"

**👨‍💻 追问预警：**
> Q: "交错（interleaved）和非交错（non-interleaved/deinterleaved）有什么区别？"
> A: 交错就是 L R L R L R 排列，非交错是先全部 L 再全部 R。扬声器/耳机要交错格式，编码器（如 AAC）要非交错/平面格式。这跟 FFmpeg 里的 Packed vs Planar 完全对应——AV_SAMPLE_FMT_S16 是交错、AV_SAMPLE_FMT_S16P 是平面。iOS AudioUnit RemoteIO 默认要交错格式，如果要平面格式需要用 AudioConverter 转。

---

#### Q3：AudioSession 是干什么的？不配会怎样？

**面试官想听什么：** 你是不是只写了采集代码，还是理解系统级音频策略。

**🗣️ 标准回答（可背诵）：**

> "AudioSession 是 App 和系统音频子系统之间的协调层，负责声明你的 App 想要什么样的音频行为。不配的话用系统默认值——这意味着电话来了你的音频不会自动暂停、锁屏就静音、插拔耳机路由可能不切换。标准的直播/RTC 配置是：category 设为 PlayAndRecord（既播又录），mode 设为 VoiceChat 或 VideoChat（系统会做回声消除优化），options 加 DefaultToSpeaker（默认外放）和 AllowBluetooth（支持蓝牙耳机）。另外要监听两个通知：AVAudioSessionInterruptionNotification（电话/Alarm 中断你）和 AVAudioSessionRouteChangeNotification（插拔耳机），在中断开始/结束时正确地暂停/恢复 AudioUnit。"

**👨‍💻 追问预警：**
> Q: "category 里的 PlayAndRecord 和 Playback 有什么区别？"
> A: Playback 只有播放没有录音——不触发麦克风权限弹窗、系统不会分配录音资源。PlayAndRecord 才启用麦克风输入。选了 PlayAndRecord 还必须处理 AudioSession 的 overrideOutputAudioPort 来控制是听筒还是外放——这个是移动端特有的坑，不设的话默认走听筒，声音很小。

---

#### Q4：RTC 场景下的回声消除怎么做？

**面试官想听什么：** 你是不是只会搭基本链路，还是理解实时通信的特殊需求。

**🗣️ 标准回答（可背诵）：**

> "在 iOS 上做 RTC 的回声消除，最简单的方式是直接把 RemoteIO 换成 VoiceProcessingIO。它是 RemoteIO 的超集，系统在内核层就做了 AEC（回声消除）、AGC（自动增益控制）和降噪。API 层面一模一样——也是 input callback 和 render callback，只是底层多了一个 DSP 处理模块。注意 VoiceProcessingIO 只支持单声道、采样率有一些设备限制。如果需要多声道或者自定义 AEC 算法，那就回到 RemoteIO，自己实现 AEC——但这复杂度极大，绝大多数场景直接用系统的 VoiceProcessingIO 就够了。这一点和 Android 的 AcousticEchoCanceler 效果器是对应的。"

**👨‍💻 追问预警：**
> Q: "VoiceProcessingIO 有什么限制？"
> A: 一是只支持单声道（mono），二是采样率有限制（通常是 8k-48k 但某些设备只支持特定采样率），三是它内部引入了一些不可控的延迟（通常 20-40ms），需要在延时和回声消除效果之间做取舍。

---

## 三、核心代码：RemoteIO 采集 + 播放

```objc
#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>

// ---- 全局变量 ----
static AudioUnit _audioUnit;
// 一个简单的 ring buffer（生产环境用 TPCircularBuffer 或自己实现的无锁版）
static uint8_t _pcmBuffer[48000 * 2 * 2]; // 1 秒缓冲（48kHz, 16bit, mono）

// ---- 1. 配置 AudioSession ----
- (void)setupAudioSession {
    AVAudioSession *session = [AVAudioSession sharedInstance];
    NSError *error = nil;

    // category: 既要播又要录
    [session setCategory:AVAudioSessionCategoryPlayAndRecord
                    mode:AVAudioSessionModeVideoChat
                 options:AVAudioSessionCategoryOptionDefaultToSpeaker |
                         AVAudioSessionCategoryOptionAllowBluetooth
                   error:&error];

    // 采样率（一般设 48kHz，某些设备可能只能到 44.1k）
    [session setPreferredSampleRate:48000 error:&error];
    // I/O buffer 时长（越小延迟越低，但太小 CPU 扛不住，0.005-0.01 是常见选择）
    [session setPreferredIOBufferDuration:0.005 error:&error];
    [session setActive:YES error:&error];
}

// ---- 2. 填写 ASBD ----
- (AudioStreamBasicDescription)createASBD {
    AudioStreamBasicDescription asbd;
    memset(&asbd, 0, sizeof(asbd));

    asbd.mSampleRate       = 48000;           // 48kHz
    asbd.mFormatID         = kAudioFormatLinearPCM;
    asbd.mFormatFlags      = kLinearPCMFormatFlagIsSignedInteger
                            | kLinearPCMFormatFlagIsPacked;
    asbd.mBitsPerChannel   = 16;              // 16 bit
    asbd.mChannelsPerFrame = 1;               // 单声道
    asbd.mFramesPerPacket  = 1;               // PCM 1 帧 = 1 packet
    asbd.mBytesPerFrame    = 2;               // (16/8) * 1 = 2
    asbd.mBytesPerPacket   = 2;

    return asbd;
}

// ---- 3. 创建 AudioUnit ----
- (void)setupAudioUnit {
    // 描述：输出类型 + RemoteIO 子类型
    AudioComponentDescription desc = {
        .componentType         = kAudioUnitType_Output,
        .componentSubType      = kAudioUnitSubType_RemoteIO, // ⚠️ RemoteIO
        .componentManufacturer = kAudioUnitManufacturer_Apple,
        .componentFlags        = 0,
        .componentFlagsMask    = 0
    };

    AudioComponent component = AudioComponentFindNext(NULL, &desc);
    AudioComponentInstanceNew(component, &_audioUnit);

    // 配置输入（Bus 1 = 麦克风）
    UInt32 enableInput = 1;
    AudioUnitSetProperty(_audioUnit,
                         kAudioOutputUnitProperty_EnableIO,
                         kAudioUnitScope_Input,
                         1,   // Bus 1
                         &enableInput,
                         sizeof(enableInput));

    // 配置输出（Bus 0 = 扬声器，默认已启用）

    // 设置流格式（输入和输出各设一次）
    AudioStreamBasicDescription asbd = [self createASBD];
    AudioUnitSetProperty(_audioUnit,
                         kAudioUnitProperty_StreamFormat,
                         kAudioUnitScope_Output,  // 注意是 Output scope！
                         1,   // Bus 1（输入）
                         &asbd,
                         sizeof(asbd));
    AudioUnitSetProperty(_audioUnit,
                         kAudioUnitProperty_StreamFormat,
                         kAudioUnitScope_Input,   // 注意是 Input scope！
                         0,   // Bus 0（输出）
                         &asbd,
                         sizeof(asbd));

    // ⚠️ scope 容易搞混：
    // - 麦克风数据流：硬件 → input scope(Bus1) → output scope(Bus1) → 你的 input callback
    // - 扬声器数据流：你的 render callback → input scope(Bus0) → output scope(Bus0) → 硬件

    // ---- 4. 设置回调 ----
    // Input callback（麦克风 → 你的 App）
    AURenderCallbackStruct inputCB;
    inputCB.inputProc = InputCallback;
    inputCB.inputProcRefCon = (__bridge void *)self;
    AudioUnitSetProperty(_audioUnit,
                         kAudioOutputUnitProperty_SetInputCallback,
                         kAudioUnitScope_Global,
                         1,   // Bus 1
                         &inputCB,
                         sizeof(inputCB));

    // Render callback（你的 App → 扬声器）
    AURenderCallbackStruct renderCB;
    renderCB.inputProc = RenderCallback;
    renderCB.inputProcRefCon = (__bridge void *)self;
    AudioUnitSetProperty(_audioUnit,
                         kAudioUnitProperty_SetRenderCallback,
                         kAudioUnitScope_Global,
                         0,   // Bus 0
                         &renderCB,
                         sizeof(renderCB));

    // 初始化
    AudioUnitInitialize(_audioUnit);
}

// ---- 5. Input Callback（系统给 PCM 数据）----
static OSStatus InputCallback(void *inRefCon,
                               AudioUnitRenderActionFlags *ioActionFlags,
                               const AudioTimeStamp *inTimeStamp,
                               UInt32 inBusNumber,
                               UInt32 inNumberFrames,
                               AudioBufferList *ioData) {
    // ⚠️ 这个回调在音频线程上！不能：加锁、alloc、syscall、print
    // ⚠️ 这个回调里 ioData 是没有数据的，需要你调 AudioUnitRender 去取

    AudioBufferList bufferList;
    bufferList.mNumberBuffers = 1;
    bufferList.mBuffers[0].mDataByteSize = inNumberFrames * 2; // 16bit mono
    bufferList.mBuffers[0].mNumberChannels = 1;
    bufferList.mBuffers[0].mData = malloc(inNumberFrames * 2); // ⚠️ 实际项目不要 malloc！用预分配 ring buffer

    OSStatus status = AudioUnitRender(_audioUnit,
                                       ioActionFlags,
                                       inTimeStamp,
                                       1,    // Bus 1
                                       inNumberFrames,
                                       &bufferList);
    if (status != noErr) {
        free(bufferList.mBuffers[0].mData);
        return status;
    }

    // ✅ 把 PCM 写入 ring buffer / 无锁队列，音频线程立即返回
    // ringBuffer_write(_ringBuffer, bufferList.mBuffers[0].mData, inNumberFrames * 2);

    free(bufferList.mBuffers[0].mData); // 现实项目中不要 free，数据已入 ring buffer
    return noErr;
}

// ---- 6. Render Callback（系统问你要 PCM 数据去播）----
static OSStatus RenderCallback(void *inRefCon,
                                AudioUnitRenderActionFlags *ioActionFlags,
                                const AudioTimeStamp *inTimeStamp,
                                UInt32 inBusNumber,
                                UInt32 inNumberFrames,
                                AudioBufferList *ioData) {
    // ⚠️ 这个回调在音频线程上！不能加锁。
    // ioData->mBuffers[0].mData 是系统给你的缓冲区——你把 PCM 填进去就行

    // ✅ 从 ring buffer 读出数据
    // int32_t bytesRead = ringBuffer_read(_ringBuffer, ioData->mBuffers[0].mData,
    //                                      inNumberFrames * 2);
    // if (bytesRead < inNumberFrames * 2) {
    //     memset(ioData->mBuffers[0].mData + bytesRead, 0,
    //            inNumberFrames * 2 - bytesRead);  // 补静音
    // }

    // 如果 ring buffer 为空，把整个 buffer 填静音——不要什么都不干，否则爆音
    memset(ioData->mBuffers[0].mData, 0, inNumberFrames * 2);
    return noErr;
}

// ---- 7. 启停 ----
- (void)startAudio {
    AudioOutputUnitStart(_audioUnit);
}

- (void)stopAudio {
    AudioOutputUnitStop(_audioUnit);
}
```

---

## 四、核心踩坑

### 坑 1：scope 和 bus 的对应关系是最容易写错的

**RemoteIO 的 scope/bus 记忆口诀**：

| 方向 | Bus | 设 StreamFormat 时的 Scope | 设回调时的 Property |
|------|-----|--------------------------|-------------------|
| 麦克风输入 | Bus 1 | **Output** scope | kAudioOutputUnitProperty_**SetInputCallback** |
| 扬声器输出 | Bus 0 | **Input** scope | kAudioUnitProperty_**SetRenderCallback** |

为什么这么反直觉？因为是从 AudioUnit 的视角看的——stream format 描述的是"从这个 scope 流出来的数据是什么格式"：
- 麦克风数据从 AudioUnit 的 output scope 流出来给你
- 你给扬声器的数据流进 AudioUnit 的 input scope

**记不住就记住一条**：AudioUnit 的 scope 视角和你（App 开发者）的视角是**反的**。

### 坑 2：回调里 alloc/free 内存

音频线程是实时线程，`malloc` 可能触发系统调用、缺页中断——直接导致 buffer 超时、音频断流。**全程用预分配内存 + ring buffer**。

### 坑 3：AudioSession 的 category 配错

`AVAudioSessionCategoryPlayback` 不支持录音，`AVAudioSessionCategoryRecord` 不支持播放。要做实时双向必须用 `AVAudioSessionCategoryPlayAndRecord`。而且设完 category 必须调 `setActive:YES`，否则不生效。

### 坑 4：不监听路由变化导致用户体验灾难

用户拔掉耳机 → 音频突然从扬声器外放 → 尴尬。必须监听 `AVAudioSessionRouteChangeNotification`，在路由变化后检查当前输出设备并做相应处理。

---

## 一句话总结
> iOS 实时音频 = AudioUnit RemoteIO 双向模型 + ASBD 配好 LinearPCM/16bit + 回调里只做 ring buffer 搬运不阻塞 + AudioSession 管好中断和路由——和 Android 的 AudioRecord/AudioTrack 思路一样，但 API 更"反直觉"。

## 关联文档
- [[00-iOS音视频开发全景导读]] — iOS 媒体栈全家福
- [[01-AVFoundation采集详解]] — 视频采集（和音频配合做音视频同步）
- [[../ffmpeg/00-FFmpeg全景导读]] §6.4 — Planar vs Packed（ASBD 里的 interleaved vs non-interleaved）
- [[../ffmpeg/cpp音视频开发音频问题与面试指南]] — 音频面试通用知识（PCM/AAC/重采样）
