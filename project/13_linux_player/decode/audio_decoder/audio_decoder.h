#pragma once

/// @file audio_decoder.h
/// @brief 音频解码器。基于 FFmpeg avcodec, 输出裸 PCM.
///
/// 重采样由 AudioResampler（process 层）负责。解码器只做 decode.

#include "decode/i_decoder.h"

struct AVCodecContext;
struct AVFrame;

namespace player {

class AudioDecoder : public IDecoder {
public:
  AudioDecoder();
  ~AudioDecoder() override;

  // ── IDecoder ──────────────────────────────────────────────────────────

  int  open(AVCodecParameters* codecParams)    override;
  int  sendPacket(AVPacket* pkt)               override;
  int  recvFrame(AVFrame* frame)               override;
  void flush()                                 override;
  void close()                                 override;

  AVCodecContext* codecContext() const { return m_codecCtx; }
  bool isOpen() const { return m_codecCtx != nullptr; }

private:
  AVCodecContext* m_codecCtx  = nullptr;
  AVFrame*        m_workFrame = nullptr;
};

} // namespace player
