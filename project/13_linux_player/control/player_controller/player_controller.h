#pragma once

/// 播放器中央控制器 — IPlayer 实现, 串联所有模块.
/// v2: 音频+视频完整管线.

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
#include "render/video/opengl_renderer/sdl_video_renderer.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

struct AVPacket;

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

  PlayerState getState()    const override;
  int64_t     getPosition() const override;
  int64_t     getDuration() const override;
  bool        isPlaying()   const override;
  bool        isSeeking()   const override;
  void        setCallback(IPlayerCallback* cb) override;

private:
  int  initPipeline_(const char* url);
  void startThreads_();
  void stopThreads_();
  void teardown_();

  void demuxLoop_();
  void audioDecodeLoop_();
  void videoDecodeLoop_();
  void videoRenderLoop_();

  void changeState_(PlayerState newState);
  void notifyError_(const char* msg);
  void notifyProgress_();

  PlayerConfig     m_config;
  IPlayerCallback* m_callback = nullptr;
  std::atomic<PlayerState> m_state{PlayerState::Idle};

  // 子模块
  FFmpegDemuxer      m_demuxer;
  AudioDecoder       m_audioDecoder;
  VideoDecoder       m_videoDecoder;
  AudioResampler     m_audioResampler;
  SDL2AudioRenderer  m_audioRenderer;
  SDLVideoRenderer    m_videoRenderer;
  ClockManager       m_clockMgr;
  EventBus           m_eventBus;

  // 队列
  using PktQueue = PacketQueue<std::shared_ptr<AVPacket>>;
  std::shared_ptr<PktQueue> m_audioPktQueue;
  std::shared_ptr<PktQueue> m_videoPktQueue;

  // 线程
  std::unique_ptr<std::thread> m_demuxThread;
  std::unique_ptr<std::thread> m_audioDecodeThread;
  std::unique_ptr<std::thread> m_videoDecodeThread;
  std::unique_ptr<std::thread> m_videoRenderThread;

  std::atomic<bool> m_abortRequested{false};
  std::atomic<bool> m_seeking{false};

  // 流信息
  int     m_audioStreamIdx = -1;
  int     m_videoStreamIdx = -1;
  int64_t m_durationMs     = 0;
  double  m_frameDuration  = 0.04;  // 25fps default
};

} // namespace player
