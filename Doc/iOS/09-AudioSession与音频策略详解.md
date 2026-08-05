# AudioSession 与 iOS 音频策略详解：中断、路由、后台

## 0. 本篇定位

- 面试复习：先掌握 category、mode、option、路由变化、中断恢复、后台音频和蓝牙策略。
- 深入学习：重点看 `AVAudioSession` 与 AudioUnit/AVAudioEngine/WebRTC 音频模块之间的职责边界。
- 工程落点：音频策略配置错误会表现为无声、延迟异常、AEC 失效或后台中断恢复失败，是移动端音视频高频故障源。
> **适用方向**：iOS 音视频 SDK 开发，直播/RTC/播放器方向
> **前置知识**：AudioUnit 基础（RemoteIO），了解 AVAudioSession 的基本概念
> **难度**：⭐⭐⭐（1-5 星）
> **预计阅读**：速记 8 分钟｜全文 25 分钟
> **关联文档**：[[00-iOS音视频开发全景导读]] · [[02-AudioUnit与音频处理详解]] · [[08-端到端采集编码推流管线]]
> **定位**：🟡 高级加分 —— AudioSession 配错了，轻则用户体验差（听筒外放/锁屏静音），重则 App Store 审核被拒

---

## 一、全景导读：AudioSession 是什么

### 1.1 一句话定义

**AudioSession 是你的 App 和 iOS 音频系统之间的「合同」**——你通过它告诉系统："我要录音、也要播放、电话来了你可以打断我、耳机拔了通知我"。系统根据这份合同来管理所有 App 的音频行为。

### 1.2 不配 AudioSession 会怎样？

| 症状 | 根因 |
|------|------|
| 锁屏后声音停了 | 默认 category 不支持后台播放 |
| 电话来了你的声音还在播 | 没配 interruption 处理 |
| 插拔耳机音频路由不切换 | 没监听 RouteChange 通知 |
| 录音声音从听筒出来、很小 | PlayAndRecord 没设 DefaultToSpeaker |
| 其他 App 播音乐时你录音失败 | category 没设对混音策略 |
| 蓝牙耳机没声音 | 没加 AllowBluetooth option |
| iPad 上录音扬声器不工作 | category/mode 不匹配 |

### 1.3 AudioSession 和 AudioUnit 的关系

```
┌──────────────────────────────────────┐
│          AVAudioSession              │  ← "签合同"：告诉系统你的音频策略
│  Category / Mode / Options           │
│  Interruption / RouteChange 通知     │
└──────────────┬───────────────────────┘
               │ 全局影响
               ▼
┌──────────────────────────────────────┐
│          AudioUnit (RemoteIO)        │  ← "干活"：实际处理 PCM 数据
│  Input callback / Render callback    │
└──────────────────────────────────────┘
```

AudioSession 管策略（能不能播、能不能录、中断了怎么办），AudioUnit 管执行（实际的音频 I/O）。两者必须配合。

---

## 二、面试速记

### 2.1 高频考点速查

| # | 考点 | 一句话答案 | 频率 | 难度 |
|---|------|-----------|------|------|
| 1 | Category 怎么选 | PlayAndRecord(实时双向) / Playback(只播) / Record(只录) | 🔥🔥🔥 | 🟢 |
| 2 | 为什么声音从听筒出来 | PlayAndRecord 默认走听筒，要加 DefaultToSpeaker option | 🔥🔥🔥 | 🟢 |
| 3 | 电话来了怎么处理 | 监听 InterruptionNotification，begin 时暂停 AudioUnit，end 时恢复 | 🔥🔥🔥 | 🟡 |
| 4 | 插拔耳机怎么处理 | 监听 RouteChangeNotification，检查 reason 和当前输出设备 | 🔥🔥 | 🟡 |
| 5 | 怎么支持后台播放 | UIBackgroundModes 加 audio + category 选 Playback | 🔥🔥 | 🟢 |
| 6 | 怎么和其他 App 混音 | category options 加 MixWithOthers | 🔥🔥 | 🟡 |
| 7 | 蓝牙支持怎么加 | PlayAndRecord options 加 AllowBluetooth / AllowBluetoothA2DP | 🔥🔥 | 🟡 |

### 2.2 面试标准回答

#### Q1：iOS 做直播/RTC 时 AudioSession 怎么配？

**面试官想听什么：** 你是否理解实时双向音频的完整配置。

**🗣️ 标准回答（可背诵）：**

> "直播/RTC 的 AudioSession 标配是：category 选 PlayAndRecord——因为既要录又要播；mode 选 VideoChat 或 VoiceChat——系统会针对通话场景做优化（回声消除、降采样等）；options 加三个：DefaultToSpeaker 让声音默认走外放而不是听筒、AllowBluetooth 支持蓝牙耳机、AllowBluetoothA2DP 支持高音质蓝牙。采样率设 48kHz、buffer duration 设 5-10ms（低延迟）。配完 category 必须调 setActive:YES 才生效。然后要注册两个通知：InterruptionNotification（电话/Facetime 打断时暂停和恢复 AudioUnit）、RouteChangeNotification（插拔耳机时检查当前路由并调整输出）。最后在 Info.plist 里声明麦克风权限。这就是一个生产级直播 App 的 AudioSession 标准配置。"

**👨‍💻 追问预警：**
> Q: "Category 选 PlayAndRecord 但不加 DefaultToSpeaker 会怎样？"
> A: 声音默认从听筒（receiver）出来——音量极小，用户以为声音坏了。这是 iOS 端最容易犯的低级错误之一。而且即使用户手动把音量调到最大，从听筒出来的声音也远小于扬声器。

---

#### Q2：中断处理怎么做？为什么不能在中断回调里做太多事？

**面试官想听什么：** 你理解中断的时序和线程约束。

**🗣️ 标准回答（可背诵）：**

> "中断分开始和结束两阶段。中断开始时（InterruptionBegan），系统通知你音频被其他高优先级事件打断——通常是电话、闹钟、Facetime——你应该立即暂停 AudioUnit，但不要销毁它。中断结束时（InterruptionEnded），系统通知你可以恢复音频了——options 里会有一个 ShouldResume 标志，表示系统建议你恢复播放。如果没有这个标志（比如被 Siri 打断后用户没选择回到你的 App），就不要自动恢复。中断通知在主线程触发，但你恢复 AudioUnit 的操作（AudioOutputUnitStart）可以 dispatch 到音频相关线程做。中断期间不要持有系统资源的引用不释放——比如录音的文件句柄、网络连接——因为中断时长不可控。"

**👨‍💻 追问预警：**
> Q: "App 进后台后 AudioUnit 还能跑吗？"
> A: 取决于 category。PlayAndRecord 在后台理论上可以跑（需要开启 Background Modes 的 audio），但实际受系统限制——后台运行时间有限制（通常 3 分钟，iOS 13 后更严格），之后会被系统挂起。长时间后台录音需要特殊 entitlement。

---

## 三、核心 Demo：完整的 AudioSession 管理器

### 3.1 AudioSessionManager.h

```objc
//  AudioSessionManager.h
//  生产级 iOS AudioSession 管理器
//
//  职责:
//  - Category/Mode/Options 配置
//  - 中断处理 (电话/Facetime/Alarm)
//  - 路由变化处理 (插拔耳机/蓝牙)
//  - 后台/前台切换
//  - 采样率和 buffer duration 协商

#import <AVFoundation/AVFoundation.h>

NS_ASSUME_NONNULL_BEGIN

/// AudioSession 状态
typedef NS_ENUM(NSInteger, AudioSessionState) {
    AudioSessionStateInactive,      // 未激活
    AudioSessionStateActive,        // 正常工作中
    AudioSessionStateInterrupted,   // 被中断（电话等）
};

@protocol AudioSessionManagerDelegate <NSObject>
@optional
/// 中断开始（电话来了等），应暂停 AudioUnit
- (void)audioSessionDidBeginInterruption;
/// 中断结束，可以恢复 AudioUnit
- (void)audioSessionDidEndInterruption:(BOOL)shouldResume;
/// 音频路由变化
- (void)audioSessionRouteDidChange:(NSString *)reason
                    currentOutputs:(NSArray<AVAudioSessionPortDescription *> *)outputs;
/// 媒体服务被重置（极少见但致命）
- (void)audioSessionMediaServicesWereReset;
@end

@interface AudioSessionManager : NSObject

@property (nonatomic, weak) id<AudioSessionManagerDelegate> delegate;
@property (nonatomic, assign, readonly) AudioSessionState state;
@property (nonatomic, assign, readonly) double currentSampleRate;
@property (nonatomic, assign, readonly) double currentIOBufferDuration;

/// 配置为直播/RTC 模式（最常用）
- (BOOL)configureForLiveStreaming;

/// 配置为纯播放模式（如播放器）
- (BOOL)configureForPlayback;

/// 配置为纯录制模式
- (BOOL)configureForRecording;

/// 自定义配置
- (BOOL)configureWithCategory:(AVAudioSessionCategory)category
                         mode:(AVAudioSessionMode)mode
                      options:(AVAudioSessionCategoryOptions)options
                   sampleRate:(double)sampleRate
            ioBufferDuration:(NSTimeInterval)duration;

/// 激活
- (BOOL)activate;

/// 反激活
- (BOOL)deactivate;

/// 查询当前输出设备
- (NSArray<AVAudioSessionPortDescription *> *)currentOutputs;

/// 是否外放
- (BOOL)isSpeakerOutput;

/// 是否蓝牙
- (BOOL)isBluetoothOutput;

/// 是否耳机
- (BOOL)isHeadphoneOutput;

@end

NS_ASSUME_NONNULL_END
```

### 3.2 AudioSessionManager.m

```objc
//  AudioSessionManager.m
//  完整的 AudioSession 管理器实现

#import "AudioSessionManager.h"

@implementation AudioSessionManager {
    AVAudioSession *_session;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _session = [AVAudioSession sharedInstance];
        _state = AudioSessionStateInactive;
        [self registerNotifications];
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

// ============================================================
// MARK: - 直播/RTC 标准配置（★最常用★）
// ============================================================
- (BOOL)configureForLiveStreaming {
    return [self configureWithCategory:AVAudioSessionCategoryPlayAndRecord
                                  mode:AVAudioSessionModeVideoChat
                               options:AVAudioSessionCategoryOptionDefaultToSpeaker |
                                       AVAudioSessionCategoryOptionAllowBluetooth |
                                       AVAudioSessionCategoryOptionAllowBluetoothA2DP
                            sampleRate:48000
                     ioBufferDuration:0.005];  // 5ms, 低延迟
}

// ============================================================
// MARK: - 纯播放配置
// ============================================================
- (BOOL)configureForPlayback {
    return [self configureWithCategory:AVAudioSessionCategoryPlayback
                                  mode:AVAudioSessionModeDefault
                               options:0
                            sampleRate:48000
                     ioBufferDuration:0.01];  // 10ms, 播放对延迟不太敏感
}

// ============================================================
// MARK: - 纯录制配置
// ============================================================
- (BOOL)configureForRecording {
    return [self configureWithCategory:AVAudioSessionCategoryRecord
                                  mode:AVAudioSessionModeDefault
                               options:AVAudioSessionCategoryOptionAllowBluetooth
                            sampleRate:48000
                     ioBufferDuration:0.005];
}

// ============================================================
// MARK: - 通用配置
// ============================================================
- (BOOL)configureWithCategory:(AVAudioSessionCategory)category
                         mode:(AVAudioSessionMode)mode
                      options:(AVAudioSessionCategoryOptions)options
                   sampleRate:(double)sampleRate
            ioBufferDuration:(NSTimeInterval)duration {

    NSError *error = nil;

    // ① 设置 Category
    if (![_session setCategory:category mode:mode options:options error:&error]) {
        NSLog(@"❌ setCategory 失败: %@", error);
        return NO;
    }

    // ② 设置采样率
    if (![_session setPreferredSampleRate:sampleRate error:&error]) {
        NSLog(@"⚠️ setPreferredSampleRate 失败: %@ (实际可能可用)", error);
    }

    // ③ 设置 I/O buffer 时长
    if (![_session setPreferredIOBufferDuration:duration error:&error]) {
        NSLog(@"⚠️ setPreferredIOBufferDuration 失败: %@ (实际可能可用)", error);
    }

    // ★ 查询实际值——系统和你的 preferred 可能不一样！
    _currentSampleRate = _session.sampleRate;
    _currentIOBufferDuration = _session.IOBufferDuration;

    NSLog(@"📢 AudioSession 配置完成:");
    NSLog(@"   Category: %@, Mode: %@", category, mode);
    NSLog(@"   实际采样率: %.0f Hz, 实际 Buffer: %.1f ms",
          _currentSampleRate, _currentIOBufferDuration * 1000);

    return YES;
}

- (BOOL)activate {
    NSError *error = nil;
    if (![_session setActive:YES error:&error]) {
        NSLog(@"❌ setActive 失败: %@", error);
        return NO;
    }
    _state = AudioSessionStateActive;
    NSLog(@"✅ AudioSession 激活");
    return YES;
}

- (BOOL)deactivate {
    NSError *error = nil;
    if (![_session setActive:NO error:&error]) {
        NSLog(@"❌ deactivate 失败: %@", error);
        return NO;
    }
    _state = AudioSessionStateInactive;
    return YES;
}

// ============================================================
// MARK: - ★ 注册系统通知 ★
// ============================================================
- (void)registerNotifications {
    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];

    // ① 中断通知（电话/Alarm/FaceTime）
    [nc addObserver:self
           selector:@selector(handleInterruption:)
               name:AVAudioSessionInterruptionNotification
             object:nil];

    // ② 路由变化通知（插拔耳机/连接蓝牙）
    [nc addObserver:self
           selector:@selector(handleRouteChange:)
               name:AVAudioSessionRouteChangeNotification
             object:nil];

    // ③ 媒体服务重置（极罕见情况——系统音频守护进程崩溃）
    [nc addObserver:self
           selector:@selector(handleMediaServicesReset:)
               name:AVAudioSessionMediaServicesWereResetNotification
             object:nil];

    // ④ App 进入后台
    [nc addObserver:self
           selector:@selector(handleAppDidEnterBackground:)
               name:UIApplicationDidEnterBackgroundNotification
             object:nil];

    // ⑤ App 回到前台
    [nc addObserver:self
           selector:@selector(handleAppWillEnterForeground:)
               name:UIApplicationWillEnterForegroundNotification
             object:nil];
}

// ============================================================
// MARK: - ★ 中断处理（最重要！）★
// ============================================================
- (void)handleInterruption:(NSNotification *)notification {
    NSDictionary *userInfo = notification.userInfo;
    AVAudioSessionInterruptionType type =
        [userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];

    if (type == AVAudioSessionInterruptionTypeBegan) {
        // 中断开始 → 暂停 AudioUnit
        _state = AudioSessionStateInterrupted;
        NSLog(@"🔴 音频中断开始");

        if ([self.delegate respondsToSelector:@selector(audioSessionDidBeginInterruption)]) {
            [self.delegate audioSessionDidBeginInterruption];
        }
    }
    else if (type == AVAudioSessionInterruptionTypeEnded) {
        // 中断结束 → 检查是否应该恢复
        AVAudioSessionInterruptionOptions options =
            [userInfo[AVAudioSessionInterruptionOptionKey] unsignedIntegerValue];

        BOOL shouldResume = (options & AVAudioSessionInterruptionOptionShouldResume);

        NSLog(@"🟢 音频中断结束, shouldResume=%d", shouldResume);

        // ★ 重新激活 AudioSession（中断期间系统可能反激活了）
        NSError *error = nil;
        [_session setActive:YES error:&error];

        if (shouldResume) {
            _state = AudioSessionStateActive;
            if ([self.delegate respondsToSelector:@selector(audioSessionDidEndInterruption:)]) {
                [self.delegate audioSessionDidEndInterruption:YES];
            }
        }
    }
}

// ============================================================
// MARK: - ★ 路由变化处理 ★
// ============================================================
- (void)handleRouteChange:(NSNotification *)notification {
    NSDictionary *userInfo = notification.userInfo;
    AVAudioSessionRouteChangeReason reason =
        [userInfo[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue];

    NSString *reasonStr = [self reasonString:reason];
    NSLog(@"🔀 音频路由变化: %@", reasonStr);

    // 获取当前输出设备
    NSArray *outputs = _session.currentRoute.outputs;

    // 特定 reason 处理
    switch (reason) {
        case AVAudioSessionRouteChangeReasonOldDeviceUnavailable: {
            // 耳机被拔掉 / 蓝牙断开
            // → 检查是否还有可用输出，可能需要切换到扬声器
            AVAudioSessionPortDescription *previous =
                userInfo[AVAudioSessionRouteChangePreviousRouteKey].outputs.firstObject;
            if ([previous.portType isEqualToString:AVAudioSessionPortHeadphones]) {
                NSLog(@"🎧 耳机被拔出，切换到扬声器");
            } else if ([previous.portType isEqualToString:AVAudioSessionPortBluetoothA2DP] ||
                       [previous.portType isEqualToString:AVAudioSessionPortBluetoothHFP]) {
                NSLog(@"🔵 蓝牙断开");
            }
            break;
        }
        case AVAudioSessionRouteChangeReasonNewDeviceAvailable: {
            // 耳机被插入 / 蓝牙连接
            break;
        }
        case AVAudioSessionRouteChangeReasonOverride: {
            // App 内部调用了 overrideOutputAudioPort
            break;
        }
        default:
            break;
    }

    if ([self.delegate respondsToSelector:@selector(audioSessionRouteDidChange:currentOutputs:)]) {
        [self.delegate audioSessionRouteDidChange:reasonStr currentOutputs:outputs];
    }
}

// ============================================================
// MARK: - 媒体服务重置
// ============================================================
- (void)handleMediaServicesReset:(NSNotification *)notification {
    NSLog(@"⚠️⚠️⚠️ 媒体服务被重置！需要重建所有音频资源");

    // 此时所有 AudioUnit/AudioQueue 等音频对象都失效了
    // 需要：销毁旧 AudioUnit → 重新激活 AudioSession → 新建 AudioUnit

    if ([self.delegate respondsToSelector:@selector(audioSessionMediaServicesWereReset)]) {
        [self.delegate audioSessionMediaServicesWereReset];
    }
}

// ============================================================
// MARK: - App 前后台切换
// ============================================================
- (void)handleAppDidEnterBackground:(NSNotification *)notification {
    NSLog(@"📱 App 进入后台");

    // PlayAndRecord + 开启了 Background Modes(audio)：可以继续跑
    // 其他 category：系统会自动暂停音频
    // 这里可以做：降低采样率/关闭不必要的音频处理 省电
}

- (void)handleAppWillEnterForeground:(NSNotification *)notification {
    NSLog(@"📱 App 回到前台");

    // 确保 AudioSession 还是激活状态
    NSError *error = nil;
    [_session setActive:YES error:&error];
}

// ============================================================
// MARK: - 查询工具
// ============================================================
- (NSArray<AVAudioSessionPortDescription *> *)currentOutputs {
    return _session.currentRoute.outputs;
}

- (BOOL)isSpeakerOutput {
    for (AVAudioSessionPortDescription *output in _session.currentRoute.outputs) {
        if ([output.portType isEqualToString:AVAudioSessionPortBuiltInSpeaker]) {
            return YES;
        }
    }
    return NO;
}

- (BOOL)isBluetoothOutput {
    for (AVAudioSessionPortDescription *output in _session.currentRoute.outputs) {
        if ([output.portType isEqualToString:AVAudioSessionPortBluetoothA2DP] ||
            [output.portType isEqualToString:AVAudioSessionPortBluetoothHFP] ||
            [output.portType isEqualToString:AVAudioSessionPortBluetoothLE]) {
            return YES;
        }
    }
    return NO;
}

- (BOOL)isHeadphoneOutput {
    for (AVAudioSessionPortDescription *output in _session.currentRoute.outputs) {
        if ([output.portType isEqualToString:AVAudioSessionPortHeadphones]) {
            return YES;
        }
    }
    return NO;
}

- (NSString *)reasonString:(AVAudioSessionRouteChangeReason)reason {
    switch (reason) {
        case AVAudioSessionRouteChangeReasonNewDeviceAvailable:     return @"新设备";
        case AVAudioSessionRouteChangeReasonOldDeviceUnavailable:   return @"旧设备移除";
        case AVAudioSessionRouteChangeReasonCategoryChange:         return @"Category 变更";
        case AVAudioSessionRouteChangeReasonOverride:               return @"App 覆盖";
        case AVAudioSessionRouteChangeReasonWakeFromSleep:          return @"唤醒";
        case AVAudioSessionRouteChangeReasonNoSuitableRouteForCategory: return @"无合适路由";
        case AVAudioSessionRouteChangeReasonRouteConfigurationChange: return @"路由配置变化";
        default: return [NSString stringWithFormat:@"未知(%ld)", (long)reason];
    }
}

@end
```

### 3.3 使用示例

```objc
// ---- 初始化 ----
AudioSessionManager *sessionMgr = [[AudioSessionManager alloc] init];
sessionMgr.delegate = self;

// ---- 配置 + 激活 ----
[sessionMgr configureForLiveStreaming];
[sessionMgr activate];

// ---- 响应中断 ----
- (void)audioSessionDidBeginInterruption {
    // 暂停 AudioUnit
    AudioOutputUnitStop(_audioUnit);
}

- (void)audioSessionDidEndInterruption:(BOOL)shouldResume {
    if (shouldResume) {
        // 恢复 AudioUnit
        AudioOutputUnitStart(_audioUnit);
    }
}

// ---- 响应路由变化 ----
- (void)audioSessionRouteDidChange:(NSString *)reason
                    currentOutputs:(NSArray *)outputs {
    // 检查是否蓝牙/耳机/外放
    if ([sessionMgr isBluetoothOutput]) {
        // 蓝牙：可能需要调整采样率
    }
}

// ---- 响应媒体服务重置 ----
- (void)audioSessionMediaServicesWereReset {
    // 重建所有音频资源：AudioUnit / AudioConverter 等
    [self rebuildAllAudioResources];
}

// ---- 切换外放/听筒 ----
// 外放
[[AVAudioSession sharedInstance] overrideOutputAudioPort:AVAudioSessionPortOverrideSpeaker error:nil];
// 听筒（默认）
[[AVAudioSession sharedInstance] overrideOutputAudioPort:AVAudioSessionPortOverrideNone error:nil];
```

---

## 四、Category / Mode / Options 速查表

### 4.1 Category 选型

| Category | 录音 | 播放 | 后台 | 锁屏静音 | 适用场景 |
|----------|------|------|------|---------|---------|
| `PlayAndRecord` | ✅ | ✅ | 需配 BackgroundModes | 否 | 直播/RTC/VoIP |
| `Playback` | ❌ | ✅ | ✅ | 否 | 音乐/视频播放器 |
| `Record` | ✅ | ❌ | 需配 BackgroundModes | 是 | 录音笔 |
| `SoloAmbient` | ❌ | ✅ | ❌ | 是 | 游戏音效 |
| `Ambient` | ❌ | ✅ | ❌ | 是 | 可与其他 App 混音 |

### 4.2 Mode 选型

| Mode | 适用场景 | 特殊行为 |
|------|---------|---------|
| `VideoChat` | 视频通话/直播 | 优化回声消除，支持蓝牙 |
| `VoiceChat` | 语音通话/VoIP | 同上，更侧重语音 |
| `Default` | 通用 | 无特殊优化 |
| `Measurement` | 音频测量 | 关闭所有信号处理 |
| `GameChat` | 游戏内语音 | iOS 14+ |
| `MoviePlayback` | 电影播放 | 优化多声道 |

### 4.3 Options

| Option | 作用 |
|--------|------|
| `DefaultToSpeaker` | 声音走外放而不是听筒 |
| `AllowBluetooth` | 允许蓝牙 HFP（免提） |
| `AllowBluetoothA2DP` | 允许蓝牙 A2DP（高音质立体声） |
| `MixWithOthers` | 和其他 App 混音（不独占音频） |
| `DuckOthers` | 你的声音播出时压低其他 App 的音量 |
| `InterruptSpokenAudioAndMixWithOthers` | 中断语音合成类 App（如导航），其他可混音 |
| `AllowAirPlay` | 允许 AirPlay |

---

## 五、常见坑

### 坑 1：PlayAndRecord 忘了 DefaultToSpeaker

**现象**：直播 App 声音从听筒出来，用户投诉"声音太小"。

**根因**：`AVAudioSessionCategoryPlayAndRecord` 默认输出到听筒（receiver），这是为通话设计的（贴近耳朵）。直播场景需要 `DefaultToSpeaker` option。

### 坑 2：中断后 AudioUnit 没恢复

**现象**：接完电话回来，App 画面正常但没有声音。

**根因**：中断结束时只重激活了 AudioSession（`setActive:YES`），但忘了重启 AudioUnit（`AudioOutputUnitStart`）。

### 坑 3：不检查 ShouldResume 标志盲目恢复

**现象**：被 Siri 打断后，回到 App 自动开始录音——用户投诉隐私问题。

**根因**：中断结束通知里有 `AVAudioSessionInterruptionOptionShouldResume` 标志——如果是被系统级功能打断（Siri/闹钟等），可能没有这个标志。这时不应自动恢复。

### 坑 4：蓝牙连接时采样率不匹配导致噪音

**现象**：蓝牙耳机连接后声音变样或爆音。

**根因**：蓝牙音频的采样率可能与你的 AudioUnit 配置不同。路由变化后需要重新查询实际采样率，必要时重建 AudioUnit。

### 坑 5：Category 配错导致无法录音

**现象**：选了 Playback category 但需要录音 → `AudioUnit` 初始化失败。

**根因**：`AVAudioSessionCategoryPlayback` 不支持录音——系统不会分配录音资源，也不会弹出麦克风权限弹窗。必须用 `PlayAndRecord`。

---

## 一句话总结
> AudioSession 是你的 App 和系统音频的"合同"——直播/RTC 标配 PlayAndRecord + VideoChat + DefaultToSpeaker + AllowBluetooth，采样率 48kHz、buffer 5ms。必须处理中断（暂停/恢复 AudioUnit）和路由变化（插拔耳机/蓝牙）。最常见的坑是忘了 DefaultToSpeaker（听筒发声）。

## 关联文档
- [[00-iOS音视频开发全景导读]] — iOS 媒体栈全景
- [[02-AudioUnit与音频处理详解]] — AudioUnit RemoteIO 采集与播放
- [[08-端到端采集编码推流管线]] — 音频在推流管线中的位置
