//  AudioSessionManager.h
//  iOS AudioSession 管理器 — 独立可用
//  依赖: AVFoundation.framework

#import <AVFoundation/AVFoundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, AudioSessionState) {
    AudioSessionStateInactive,
    AudioSessionStateActive,
    AudioSessionStateInterrupted,
};

@protocol AudioSessionManagerDelegate <NSObject>
@optional
- (void)audioSessionDidBeginInterruption;
- (void)audioSessionDidEndInterruption:(BOOL)shouldResume;
- (void)audioSessionRouteDidChange:(NSString *)reason
                    currentOutputs:(NSArray<AVAudioSessionPortDescription *> *)outputs;
- (void)audioSessionMediaServicesWereReset;
@end

@interface AudioSessionManager : NSObject

@property (nonatomic, weak) id<AudioSessionManagerDelegate> delegate;
@property (nonatomic, assign, readonly) AudioSessionState state;
@property (nonatomic, assign, readonly) double currentSampleRate;
@property (nonatomic, assign, readonly) double currentIOBufferDuration;

/// 直播/RTC 标准配置 (PlayAndRecord + VideoChat + DefaultToSpeaker + 48kHz + 5ms)
- (BOOL)configureForLiveStreaming;

/// 纯播放 (Playback + 48kHz + 10ms)
- (BOOL)configureForPlayback;

/// 纯录制 (Record + 48kHz + 5ms)
- (BOOL)configureForRecording;

/// 自定义配置
- (BOOL)configure:(AVAudioSessionCategory)category
             mode:(AVAudioSessionMode)mode
          options:(AVAudioSessionCategoryOptions)options
       sampleRate:(double)sr
       bufferTime:(NSTimeInterval)t;

- (BOOL)activate;
- (BOOL)deactivate;

/// 查询输出设备
- (BOOL)isSpeakerOutput;
- (BOOL)isBluetoothOutput;
- (BOOL)isHeadphoneOutput;

@end

NS_ASSUME_NONNULL_END
