//  AudioSessionManager.m — 完整实现
//  详见 Doc/iOS/09-AudioSession与音频策略详解.md 的完整讲解

#import "AudioSessionManager.h"

@implementation AudioSessionManager {
    AVAudioSession *_session;
}

- (instancetype)init {
    self = [super init]; if (!self) return nil;
    _session = [AVAudioSession sharedInstance]; _state = AudioSessionStateInactive;
    [self observe]; return self;
}
- (void)dealloc { [[NSNotificationCenter defaultCenter] removeObserver:self]; }

- (BOOL)configureForLiveStreaming {
    return [self configure:AVAudioSessionCategoryPlayAndRecord
                      mode:AVAudioSessionModeVideoChat
                   options:AVAudioSessionCategoryOptionDefaultToSpeaker |
                           AVAudioSessionCategoryOptionAllowBluetooth |
                           AVAudioSessionCategoryOptionAllowBluetoothA2DP
                sampleRate:48000 bufferTime:0.005];
}
- (BOOL)configureForPlayback {
    return [self configure:AVAudioSessionCategoryPlayback mode:AVAudioSessionModeDefault options:0 sampleRate:48000 bufferTime:0.01];
}
- (BOOL)configureForRecording {
    return [self configure:AVAudioSessionCategoryRecord mode:AVAudioSessionModeDefault options:AVAudioSessionCategoryOptionAllowBluetooth sampleRate:48000 bufferTime:0.005];
}
- (BOOL)configure:(AVAudioSessionCategory)c mode:(AVAudioSessionMode)m options:(AVAudioSessionCategoryOptions)o sampleRate:(double)sr bufferTime:(NSTimeInterval)t {
    NSError *e;
    [_session setCategory:c mode:m options:o error:&e];
    [_session setPreferredSampleRate:sr error:&e];
    [_session setPreferredIOBufferDuration:t error:&e];
    _currentSampleRate = _session.sampleRate;
    _currentIOBufferDuration = _session.IOBufferDuration;
    return YES;
}
- (BOOL)activate {
    NSError *e;
    BOOL ok = [_session setActive:YES error:&e];
    if (ok) _state = AudioSessionStateActive;
    return ok;
}
- (BOOL)deactivate {
    NSError *e;
    BOOL ok = [_session setActive:NO error:&e];
    if (ok) _state = AudioSessionStateInactive;
    return ok;
}

- (void)observe {
    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    [nc addObserver:self selector:@selector(onInterrupt:) name:AVAudioSessionInterruptionNotification object:nil];
    [nc addObserver:self selector:@selector(onRoute:) name:AVAudioSessionRouteChangeNotification object:nil];
    [nc addObserver:self selector:@selector(onReset:) name:AVAudioSessionMediaServicesWereResetNotification object:nil];
    [nc addObserver:self selector:@selector(onBg:) name:UIApplicationDidEnterBackgroundNotification object:nil];
    [nc addObserver:self selector:@selector(onFg:) name:UIApplicationWillEnterForegroundNotification object:nil];
}

- (void)onInterrupt:(NSNotification *)n {
    NSDictionary *u = n.userInfo;
    if ([u[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue] == AVAudioSessionInterruptionTypeBegan) {
        _state = AudioSessionStateInterrupted;
        if ([self.delegate respondsToSelector:@selector(audioSessionDidBeginInterruption)])
            [self.delegate audioSessionDidBeginInterruption];
    } else {
        BOOL r = ([u[AVAudioSessionInterruptionOptionKey] unsignedIntegerValue] & AVAudioSessionInterruptionOptionShouldResume);
        [_session setActive:YES error:nil];
        if (r) { _state = AudioSessionStateActive;
            if ([self.delegate respondsToSelector:@selector(audioSessionDidEndInterruption:)])
                [self.delegate audioSessionDidEndInterruption:YES]; }
    }
}
- (void)onRoute:(NSNotification *)n {
    if ([self.delegate respondsToSelector:@selector(audioSessionRouteDidChange:currentOutputs:)])
        [self.delegate audioSessionRouteDidChange:@"change" currentOutputs:_session.currentRoute.outputs];
}
- (void)onReset:(NSNotification *)n {
    if ([self.delegate respondsToSelector:@selector(audioSessionMediaServicesWereReset)])
        [self.delegate audioSessionMediaServicesWereReset];
}
- (void)onBg:(NSNotification *)n {}
- (void)onFg:(NSNotification *)n { [_session setActive:YES error:nil]; }

- (BOOL)isSpeakerOutput  { return [_session.currentRoute.outputs.firstObject.portType isEqualToString:AVAudioSessionPortBuiltInSpeaker]; }
- (BOOL)isBluetoothOutput { NSString *t = _session.currentRoute.outputs.firstObject.portType; return [t isEqualToString:AVAudioSessionPortBluetoothA2DP] || [t isEqualToString:AVAudioSessionPortBluetoothHFP]; }
- (BOOL)isHeadphoneOutput { return [_session.currentRoute.outputs.firstObject.portType isEqualToString:AVAudioSessionPortHeadphones]; }

@end
