/// @file sdl2_audio_renderer.cpp
/// @brief SDL2AudioRenderer — SDL2 音频输出实现.

#include "render/audio/sdl2_renderer/sdl2_audio_renderer.h"
#include "core/clock/clock.h"
#include "utils/logger/logger.h"

#include <cerrno>
#include <cstring>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}

namespace player {

// ── 生命周期 ────────────────────────────────────────────────────────────

SDL2AudioRenderer::SDL2AudioRenderer() {
  m_ringBuffer = std::make_unique<AudioRingBuffer>();
}

SDL2AudioRenderer::~SDL2AudioRenderer() {
  destroy();
}

// ── init ─────────────────────────────────────────────────────────────────

int SDL2AudioRenderer::init(const RenderConfig& /*cfg*/) {
  return 0;
}

// ── openDevice (eager, with explicit params) ─────────────────────────────

int SDL2AudioRenderer::openDevice(int& sampleRate, int channels, AVSampleFormat fmt) {
  if (m_deviceId != 0) return 0;

  SDL_AudioSpec desired;
  SDL_zero(desired);

  m_sampleRate = sampleRate;
  m_channels   = channels;

  switch (fmt) {
    case AV_SAMPLE_FMT_S16:
    case AV_SAMPLE_FMT_S16P:
      desired.format = AUDIO_S16SYS;
      m_bytesPerSample = m_channels * 2;
      break;
    case AV_SAMPLE_FMT_FLT:
    case AV_SAMPLE_FMT_FLTP:
      desired.format = AUDIO_F32SYS;
      m_bytesPerSample = m_channels * 4;
      break;
    default:
      desired.format = AUDIO_S16SYS;
      m_bytesPerSample = m_channels * 2;
      break;
  }

  desired.freq     = m_sampleRate;
  desired.channels = m_channels;
  desired.samples  = 1024;
  desired.callback = sdlCallback_;
  desired.userdata = this;

  SDL_AudioSpec obtained;
  // Don't use SDL_AUDIO_ALLOW_ANY_CHANGE — we want exact format match
  m_deviceId = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
  if (m_deviceId == 0) return -1;

  m_sampleRate    = obtained.freq;
  m_channels      = obtained.channels;
  m_bytesPerSample = (obtained.format == AUDIO_F32SYS) ? m_channels * 4 : m_channels * 2;
  sampleRate      = obtained.freq;

  // Device stays paused — caller unpauses after clock reset
  return 0;
}

// ── openDevice_ (lazy, from AVFrame) ─────────────────────────────────────

int SDL2AudioRenderer::openDevice_(AVFrame* frame) {
  int sr = frame->sample_rate;
  return openDevice(sr, frame->ch_layout.nb_channels,
                    static_cast<AVSampleFormat>(frame->format));
}

// ── render ──────────────────────────────────────────────────────────────

int SDL2AudioRenderer::render(AVFrame* frame) {
  if (!frame) return -EINVAL;

  if (m_deviceId == 0) {
    int ret = openDevice_(frame);
    if (ret < 0) return ret;
  }

  if (m_paused.load(std::memory_order_acquire)) return 0;

  int bytes = av_samples_get_buffer_size(
      nullptr, m_channels, frame->nb_samples,
      static_cast<AVSampleFormat>(frame->format), 1);
  if (bytes <= 0) return 0;

  // Block until ring buffer has space (don't drop audio frames)
  while (m_ringBuffer->writable() < static_cast<size_t>(bytes)) {
    SDL_Delay(1);
  }
  m_ringBuffer->write(frame->data[0], static_cast<size_t>(bytes));
  return 0;
}

// ── resize ──────────────────────────────────────────────────────────────

void SDL2AudioRenderer::resize(int /*w*/, int /*h*/) {}

// ── pause / resume ──────────────────────────────────────────────────────

void SDL2AudioRenderer::pause() {
  m_paused.store(true, std::memory_order_release);
  if (m_deviceId != 0) SDL_PauseAudioDevice(m_deviceId, 1);
}

void SDL2AudioRenderer::resume() {
  m_paused.store(false, std::memory_order_release);
  if (m_deviceId != 0) SDL_PauseAudioDevice(m_deviceId, 0);
}

// ── destroy ─────────────────────────────────────────────────────────────

void SDL2AudioRenderer::destroy() {
  if (m_deviceId != 0) {
    SDL_CloseAudioDevice(m_deviceId);
    m_deviceId = 0;
  }
  m_ringBuffer->clear();
  // SDL_QuitSubSystem handled centrally by PlayerController
}

// ── SDL Callback ────────────────────────────────────────────────────────

/// @brief SDL 音频回调 — 由 SDL 后台线程调用，声卡需要下一段 PCM 数据时触发.
///
/// 此函数签名由 SDL 库规定（SDL_AudioCallback 类型）：
///
/// @param userdata  打开设备时传入的自定义指针（desired.userdata = this），
///                  即 SDL2AudioRenderer 实例指针.
/// @param stream    SDL 分配的音频输出缓冲区，必须向其中写入 len 字节的 PCM 数据.
///                  不足部分应填 0（静音），SD​L 不会自动清零.
/// @param len       需要写入 stream 的字节数.
///                  = sample_rate × channels × bytes_per_sample × callback_interval.
///                  例如: 44100 × 2 × 2 × ~23ms ≈ 4096 bytes.
///
/// @note 回调运行在 SDL 内部线程（非调用者线程），必须快速返回，不能做 I/O、锁等待.
void SDL2AudioRenderer::sdlCallback_(void* userdata, Uint8* stream, int len) {
  auto* self = static_cast<SDL2AudioRenderer*>(userdata);
  self->onAudioCallback_(stream, len);
}

void SDL2AudioRenderer::onAudioCallback_(Uint8* stream, int len) {
  size_t read = m_ringBuffer->read(stream, static_cast<size_t>(len));

  // 不足部分填静音（SDL callback 必须写满 buffer）
  if (read < static_cast<size_t>(len)) {
    memset(stream + read, 0, static_cast<size_t>(len) - read);
  }

  // 更新音频时钟
  if (m_bytesPerSample > 0) {
    size_t samplesPlayed = static_cast<size_t>(len) / m_bytesPerSample;
    double elapsed = static_cast<double>(samplesPlayed) / m_sampleRate;
    double newClock = m_audioClock.load(std::memory_order_acquire) + elapsed;
    m_audioClock.store(newClock, std::memory_order_release);

    // Sync to external ClockManager clock if wired
    if (m_clockTarget) {
      m_clockTarget->setClock(newClock);
      static int clkCnt = 0; ++clkCnt;
      if (clkCnt <= 10 || clkCnt % 100 == 0)
        LOGD_CLOCK("cb #%d clock=%.1fms (+%.1fms)",
            clkCnt, newClock * 1000.0, elapsed * 1000.0);
    }
  }
}

} // namespace player
