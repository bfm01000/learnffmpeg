#pragma once

/// @file audio_resampler.h
/// @brief 音频重采样器。封装 libswresample, 将解码 PCM 转为目标格式.

#include <cstdint>

struct AVFrame;
struct SwrContext;

namespace player {

class AudioResampler {
public:
  AudioResampler();
  ~AudioResampler();

  AudioResampler(const AudioResampler&) = delete;
  AudioResampler& operator=(const AudioResampler&) = delete;
  AudioResampler(AudioResampler&&) = delete;
  AudioResampler& operator=(AudioResampler&&) = delete;

  /// @return 0 成功, <0 FFmpeg 错误码.
  int init(int inSampleRate, int inFormat, int inChannels,
           int outSampleRate, int outFormat, int outChannels);

  /// @return 新分配 AVFrame*（调用者 av_frame_free）, nullptr 失败.
  AVFrame* convert(AVFrame* inFrame);

  AVFrame* flush();
  void close();
  bool isOpen() const { return m_swrCtx != nullptr; }

private:
  SwrContext* m_swrCtx     = nullptr;
  int         m_outRate    = 0;
  int         m_outFormat  = -1;
  int         m_outChannels = 0;
};

} // namespace player
