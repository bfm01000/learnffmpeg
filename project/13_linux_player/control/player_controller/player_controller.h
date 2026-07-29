#pragma once

/// @file player_controller.h
/// @brief 播放器中央控制器 — IPlayer 实现, 串联所有模块.
///
/// ==========================================================================
/// 当前能力（v1, 音频优先）
/// ==========================================================================
///   完整音频链路: Demux → PacketQueue → AudioDecoder → AudioResampler
///                → SDL2AudioRenderer → 扬声器
///
///   视频链路: TODO (Decoder 已有, 缺 OpenGL Renderer)
///
/// ==========================================================================
/// 线程 (2 条)
/// ==========================================================================
///   Demux Thread:  av_read_frame → 路由 packet 到对应 PacketQueue
///   Audio Decode:  pop PacketQueue → decode → resample → render(ring buffer)
///   SDL Callback:  SDL 内部线程, 从 ring buffer 取 PCM → 播放 + 更新 AudioClock

#include "api/player.h"
#include "api/player_config.h"
#include "core/clock/clock_manager.h"
#include "core/event/event_bus.h"
#include "core/queue/packet_queue.h"
#include "source/demuxer/ffmpeg_demuxer.h"
#include "decode/audio_decoder/audio_decoder.h"
#include "decode/video_decoder/video_decoder.h"
#include "process/resampler/audio_resampler.h"
#include "render/audio/sdl2_renderer/sdl2_audio_renderer.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

struct AVPacket;

namespace player {

class PlayerController : public IPlayer {
public:
  PlayerController();
  explicit PlayerController(const PlayerConfig& config);
  ~PlayerController() override;

  // ── IPlayer ──────────────────────────────────────────────────────────

  int  open(const char* url) override;
  int  open(const char* url, const PlayerConfig& config) override;
  int  play()    override;
  int  pause()   override;
  int  stop()    override;
  int  seek(int64_t positionMs) override;
  int  setSpeed(double speed) override;
  int  setVolume(float volume) override;
  int  setLoop(bool loop) override;

  PlayerState getState()    const override;
  int64_t     getPosition() const override;
  int64_t     getDuration() const override;
  bool        isPlaying()   const override;
  bool        isSeeking()   const override;
  void        setCallback(IPlayerCallback* cb) override;

private:
  // ── 管线 ────────────────────────────────────────────────────────────

  int  initPipeline_(const char* url);
  void startThreads_();
  void stopThreads_();
  void teardown_();

  // ── 线程函数 ──────────────────────────────────────────────────────────

  void demuxLoop_();
  void audioDecodeLoop_();

  // ── 状态/事件 ────────────────────────────────────────────────────────

  void changeState_(PlayerState newState);
  void notifyError_(const char* msg);

  // ── 配置 ─────────────────────────────────────────────────────────────

  PlayerConfig      m_config;
  IPlayerCallback*  m_callback = nullptr;

  // ── 子模块 ────────────────────────────────────────────────────────────

  FFmpegDemuxer            m_demuxer;
  AudioDecoder             m_audioDecoder;
  AudioResampler           m_audioResampler;
  SDL2AudioRenderer        m_audioRenderer;
  // VideoDecoder           m_videoDecoder;    // TODO
  ClockManager             m_clockMgr;
  EventBus                 m_eventBus;
  std::atomic<PlayerState> m_state{PlayerState::Idle};

  // ── 队列 ──────────────────────────────────────────────────────────────

  using PktQueue = PacketQueue<std::shared_ptr<AVPacket>>;
  std::shared_ptr<PktQueue> m_audioPktQueue;
  // std::shared_ptr<PktQueue> m_videoPktQueue;  // TODO

  // ── 线程 ──────────────────────────────────────────────────────────────

  std::unique_ptr<std::thread> m_demuxThread;
  std::unique_ptr<std::thread> m_audioDecodeThread;

  std::atomic<bool> m_abortRequested{false};
  std::atomic<bool> m_seeking{false};

  // ── 流信息 ────────────────────────────────────────────────────────────

  int     m_audioStreamIdx = -1;
  int     m_videoStreamIdx = -1;
  int64_t m_durationMs     = 0;
};

} // namespace player
