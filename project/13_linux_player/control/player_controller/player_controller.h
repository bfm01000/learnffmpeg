#pragma once

/// 播放器中央控制器 — IPlayer 实现, 串联所有模块.
/// v3: StateMachine + FrameQueue + AVSyncEngine + SeekHandler 全链路.

struct AVPacket;  // FFmpeg C type, forward-declared for shared_ptr use

#include "api/player.h"
#include "api/player_config.h"
#include "control/av_sync_engine/av_sync_engine.h"
#include "control/seek_handler/seek_handler.h"
#include "control/state_machine/state_machine.h"
#include "core/clock/clock_manager.h"
#include "core/event/event_bus.h"
#include "core/memory/frame.h"
#include "core/queue/frame_queue.h"
#include "core/queue/packet_queue.h"
#include "source/demuxer/ffmpeg_demuxer.h"
#include "decode/audio_decoder/audio_decoder.h"
#include "decode/video_decoder/video_decoder.h"
#include "process/resampler/audio_resampler.h"
#include "render/audio/sdl2_renderer/sdl2_audio_renderer.h"
#include "render/video/opengl_renderer/sdl_video_renderer.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace player {

class PlayerController : public IPlayer {
public:
    PlayerController();
    explicit PlayerController(const PlayerConfig& config);
    ~PlayerController() override;

    int  open(const char* url) override;
    int  open(const char* url, const PlayerConfig& config) override;
    int  play()    override;
    int  pause()   override;
    int  stop()    override;
    int  seek(int64_t positionMs) override;
    int  setSpeed(double speed) override;
    int  setVolume(float volume) override;
    int  setLoop(bool loop) override;

    bool pumpEvents() override;

    PlayerState getState()    const override;
    int64_t     getPosition() const override;
    int64_t     getDuration() const override;
    bool        isPlaying()   const override;
    bool        isSeeking()   const override;
    void        setCallback(IPlayerCallback* cb) override;

private:
    int  initPipeline_(const char* url);
    void setupStateMachine_();
    void setupSeekHandler_();
    void startThreads_();
    void stopThreads_();
    void teardown_();

    void demuxLoop_();
    void audioDecodeLoop_();
    void videoDecodeLoop_();

    void notifyError_(const char* msg);
    void notifyProgress_();

    PlayerConfig     m_config;
    IPlayerCallback* m_callback = nullptr;

    // ── Control infrastructure ─────────────────────────────────────────
    StateMachine  m_stateMachine;
    ClockManager  m_clockMgr;
    AVSyncEngine  m_avSync;
    EventBus      m_eventBus;
    std::unique_ptr<SeekHandler> m_seekHandler;

    // ── Pipeline modules ───────────────────────────────────────────────
    FFmpegDemuxer      m_demuxer;
    AudioDecoder       m_audioDecoder;
    VideoDecoder       m_videoDecoder;
    AudioResampler     m_audioResampler;
    SDL2AudioRenderer  m_audioRenderer;
    SDLVideoRenderer   m_videoRenderer;

    // ── Queues ─────────────────────────────────────────────────────────
    using PktQueue = PacketQueue<std::shared_ptr<AVPacket>>;
    using FrmQueue = FrameQueue<std::shared_ptr<Frame>>;

    std::shared_ptr<PktQueue> m_audioPktQueue;
    std::shared_ptr<PktQueue> m_videoPktQueue;
    std::shared_ptr<FrmQueue> m_videoFrmQueue;

    // ── Threads ────────────────────────────────────────────────────────
    std::unique_ptr<std::thread> m_demuxThread;
    std::unique_ptr<std::thread> m_audioDecodeThread;
    std::unique_ptr<std::thread> m_videoDecodeThread;

    std::atomic<bool> m_abortRequested{false};
    bool m_dropVideoUntilKeyframe = false;  // demuxLoop_ 丢帧恢复标志

    // ── Stream info ────────────────────────────────────────────────────
    int     m_audioStreamIdx = -1;
    int     m_videoStreamIdx = -1;
    int64_t m_durationMs     = 0;
    double  m_frameDuration  = 0.04;  // 25fps default

    // ── Playback state ─────────────────────────────────────────────────
    float  m_volume      = 1.0f;
    bool   m_loop        = false;
    double m_speed       = 1.0;
    std::chrono::steady_clock::time_point m_playStart{}; // play() wall time

    // ── Per-instance log counters (was static locals — shared across instances) ─
    int m_logCntAudio  = 0;  // audio decode frame counter
    int m_logCntVideo  = 0;  // video decode frame counter
    int m_logCntRender = 0;  // rendered frame counter
    int m_logCntDrop   = 0;  // dropped frame counter
    int m_logCntSkip   = 0;  // skipped packet counter (ahead of clock)
};

} // namespace player
