/// @file audio_resampler.cpp
/// @brief AudioResampler — libswresample 封装.

#include "process/resampler/audio_resampler.h"

#include <cerrno>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>
}

namespace player {

AudioResampler::AudioResampler() = default;

AudioResampler::~AudioResampler() {
  close();
}

// ── init ─────────────────────────────────────────────────────────────────

int AudioResampler::init(int inSampleRate, int inFormat, int inChannels,
                         int outSampleRate, int outFormat, int outChannels) {
  close();

  AVChannelLayout inChLayout{};
  AVChannelLayout outChLayout{};
  av_channel_layout_default(&inChLayout,  inChannels);
  av_channel_layout_default(&outChLayout, outChannels);

  int ret = swr_alloc_set_opts2(&m_swrCtx,
                                &outChLayout, static_cast<AVSampleFormat>(outFormat), outSampleRate,
                                &inChLayout,  static_cast<AVSampleFormat>(inFormat),  inSampleRate,
                                0, nullptr);
  av_channel_layout_uninit(&inChLayout);
  av_channel_layout_uninit(&outChLayout);

  if (ret < 0 || !m_swrCtx) {
    close();
    return ret < 0 ? ret : AVERROR(ENOMEM);
  }

  ret = swr_init(m_swrCtx);
  if (ret < 0) { close(); return ret; }

  m_outRate     = outSampleRate;
  m_outFormat   = outFormat;
  m_outChannels = outChannels;

  return 0;
}

// ── helpers ──────────────────────────────────────────────────────────────

namespace {

/// 分配音频帧缓冲区。用 av_samples_alloc 直接分配（绕过 av_frame_get_buffer）.
bool allocAudioBuffer(AVFrame* frame, int channels, int samples, AVSampleFormat fmt) {
  // 先释放旧 buffer
  av_freep(&frame->data[0]);
  frame->extended_data = nullptr;

  int ret = av_samples_alloc(frame->data, frame->linesize,
                              channels, samples, fmt, 0);
  if (ret < 0) return false;

  frame->extended_data = frame->data;
  return true;
}

} // anonymous namespace

// ── convert ─────────────────────────────────────────────────────────────

AVFrame* AudioResampler::convert(AVFrame* inFrame) {
  if (!m_swrCtx || !inFrame || inFrame->nb_samples <= 0) return nullptr;

  int outSamples = swr_get_out_samples(m_swrCtx, inFrame->nb_samples);
  if (outSamples <= 0) return nullptr;

  AVFrame* outFrame = av_frame_alloc();
  if (!outFrame) return nullptr;

  outFrame->sample_rate    = m_outRate;
  outFrame->format         = m_outFormat;
  outFrame->nb_samples     = outSamples;
  av_channel_layout_default(&outFrame->ch_layout, m_outChannels);

  if (!allocAudioBuffer(outFrame, m_outChannels, outSamples,
                         static_cast<AVSampleFormat>(m_outFormat))) {
    av_frame_free(&outFrame);
    return nullptr;
  }

  int converted = swr_convert(m_swrCtx,
                              outFrame->data, outSamples,
                              const_cast<const uint8_t**>(inFrame->data),
                              inFrame->nb_samples);
  if (converted < 0) {
    av_frame_free(&outFrame);
    return nullptr;
  }

  outFrame->nb_samples = converted;
  outFrame->pts        = inFrame->pts;
  outFrame->pkt_dts    = inFrame->pkt_dts;
  outFrame->time_base  = inFrame->time_base;

  return outFrame;
}

// ── flush ───────────────────────────────────────────────────────────────

AVFrame* AudioResampler::flush() {
  if (!m_swrCtx) return nullptr;

  AVFrame* outFrame = av_frame_alloc();
  if (!outFrame) return nullptr;

  outFrame->sample_rate = m_outRate;
  outFrame->format      = m_outFormat;
  outFrame->nb_samples  = 4096;
  av_channel_layout_default(&outFrame->ch_layout, m_outChannels);

  if (!allocAudioBuffer(outFrame, m_outChannels, 4096,
                         static_cast<AVSampleFormat>(m_outFormat))) {
    av_frame_free(&outFrame);
    return nullptr;
  }

  int flushed = swr_convert(m_swrCtx, outFrame->data, 4096, nullptr, 0);
  if (flushed <= 0) {
    av_frame_free(&outFrame);
    return nullptr;
  }

  outFrame->nb_samples = flushed;
  return outFrame;
}

// ── close ────────────────────────────────────────────────────────────────

void AudioResampler::close() {
  swr_free(&m_swrCtx);
  m_outRate     = 0;
  m_outFormat   = -1;
  m_outChannels = 0;
}

} // namespace player
