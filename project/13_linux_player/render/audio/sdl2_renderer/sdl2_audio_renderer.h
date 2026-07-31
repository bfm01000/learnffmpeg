#pragma once

/// @file sdl2_audio_renderer.h
/// @brief SDL2 音频渲染器。SDL callback 从 AudioRingBuffer 取 PCM 播放.
///
/// Thread safety:  render() 由 Audio Decode Thread 调用,
///                 SDL callback 由 SDL 内部线程调用,
///                 AudioRingBuffer 保证两者的无锁安全.
///
/// ==========================================================================
/// 时钟
/// ==========================================================================
///   每次 SDL callback 消费数据后更新 audio_clock.
///   audio_clock = 已播放的总采样数 / 采样率.
///   通过 getAudioClock() 暴露, AVSyncEngine 用它做 Master Clock.
///
/// ==========================================================================
/// 延迟打开
/// ==========================================================================
///   设备在首次 render() 时根据帧参数延迟打开。init() 仅创建 ring buffer.

#include "render/i_renderer.h"
#include "render/audio/sdl2_renderer/audio_ring_buffer.h"

#include <SDL2/SDL.h>
#include <atomic>
#include <memory>

namespace player {

// fwd
class Clock;

class SDL2AudioRenderer : public IRenderer {
public:
  SDL2AudioRenderer();
  ~SDL2AudioRenderer() override;

  SDL2AudioRenderer(const SDL2AudioRenderer&) = delete;
  SDL2AudioRenderer& operator=(const SDL2AudioRenderer&) = delete;
  SDL2AudioRenderer(SDL2AudioRenderer&&) = delete;
  SDL2AudioRenderer& operator=(SDL2AudioRenderer&&) = delete;

  // ── IRenderer ────────────────────────────────────────────────────────

  int  init(const RenderConfig& cfg) override;
  int  render(AVFrame* frame) override;
  void resize(int w, int h) override;   // 音频无需 resize, 空实现
  void destroy() override;

  // ── 音频控制 ──────────────────────────────────────────────────────────

  void pause();
  void resume();

  /// 获取当前播放位置（秒）, 由 SDL callback 更新.
  double getAudioClock() const { return m_audioClock.load(std::memory_order_acquire); }

  /// 设置外部 Clock 对象, SDL callback 会将播放进度同步到该 clock.
  /// 传 nullptr 则取消同步.
  void setClockTarget(Clock* clock) { m_clockTarget = clock; }

  /// 预先打开音频设备（必须在 SDL_CreateWindow 之前调用）.
  /// 之后 render() 不再触发延迟打开，直接写入 ring buffer.
  /// @param[in,out] sampleRate  输入目标采样率，输出设备实际采样率
  /// @param[in]     channels    目标声道数
  /// @param[in]     format      目标采样格式
  int openDevice(int& sampleRate, int channels, AVSampleFormat format);

private:
  /// 根据帧参数（首次打开时使用）打开 SDL 设备
  int openDevice_(AVFrame* frame);

  /// SDL 音频回调 — 从 RingBuffer 取数据喂给 SDL
  static void sdlCallback_(void* userdata, Uint8* stream, int len);
  void onAudioCallback_(Uint8* stream, int len);

  SDL_AudioDeviceID          m_deviceId = 0;
  std::unique_ptr<AudioRingBuffer> m_ringBuffer;
  std::atomic<double>        m_audioClock{0.0};
  std::atomic<bool>          m_paused{false};
  Clock*                     m_clockTarget = nullptr; // non-owning

  int m_sampleRate = 0;
  int m_channels   = 0;
  int m_bytesPerSample = 0;    // 每帧采样字节数（用于时钟计算）
};

} // namespace player
