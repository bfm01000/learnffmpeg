/// @file sdl2_audio_renderer.cpp
/// @brief SDL2AudioRenderer — SDL2 音频输出实现.

#include "render/audio/sdl2_renderer/sdl2_audio_renderer.h"

#include <cerrno>
#include <cstring>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}

namespace player {

// ── 生命周期 ────────────────────────────────────────────────────────────

SDL2AudioRenderer::SDL2AudioRenderer() {
  SDL_InitSubSystem(SDL_INIT_AUDIO);
  m_ringBuffer = std::make_unique<AudioRingBuffer>();
}

SDL2AudioRenderer::~SDL2AudioRenderer() {
  destroy();
}

// ── init ─────────────────────────────────────────────────────────────────

int SDL2AudioRenderer::init(const RenderConfig& /*cfg*/) {
  return 0;
}

// ── openDevice_ ──────────────────────────────────────────────────────────

int SDL2AudioRenderer::openDevice_(AVFrame* frame) {
  if (m_deviceId != 0) return 0;

  SDL_AudioSpec desired;
  SDL_zero(desired);

  m_sampleRate = frame->sample_rate;
  m_channels   = frame->ch_layout.nb_channels;

  switch (frame->format) {
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
  m_deviceId = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained,
                                    SDL_AUDIO_ALLOW_ANY_CHANGE);
  if (m_deviceId == 0) return -1;

  m_sampleRate = obtained.freq;
  m_channels   = obtained.channels;
  if (obtained.format == AUDIO_S16SYS) {
    m_bytesPerSample = m_channels * 2;
  } else if (obtained.format == AUDIO_F32SYS) {
    m_bytesPerSample = m_channels * 4;
  }

  SDL_PauseAudioDevice(m_deviceId, 0);
  return 0;
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

  size_t written = m_ringBuffer->write(frame->data[0], static_cast<size_t>(bytes));
  return written > 0 ? 0 : -1;
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
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

// ── SDL Callback ────────────────────────────────────────────────────────

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
    double clock = m_audioClock.load(std::memory_order_acquire);
    m_audioClock.store(clock + elapsed, std::memory_order_release);
  }
}

} // namespace player
