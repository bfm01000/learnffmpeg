/// @file audio_decoder.cpp
/// @brief AudioDecoder — 基于 FFmpeg 的软件音频解码器（纯解码, 无重采样）.

#include "decode/audio_decoder/audio_decoder.h"

#include <cerrno>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
}

namespace player {

// ── 生命周期 ────────────────────────────────────────────────────────────

AudioDecoder::AudioDecoder() = default;

AudioDecoder::~AudioDecoder() {
  close();
}

// ── open ─────────────────────────────────────────────────────────────────

int AudioDecoder::open(AVCodecParameters* codecParams) {
  if (!codecParams) return -EINVAL;

  close();

  const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
  if (!codec) return AVERROR_DECODER_NOT_FOUND;

  m_codecCtx = avcodec_alloc_context3(codec);
  if (!m_codecCtx) return AVERROR(ENOMEM);

  int ret = avcodec_parameters_to_context(m_codecCtx, codecParams);
  if (ret < 0) { close(); return ret; }

  ret = avcodec_open2(m_codecCtx, codec, nullptr);
  if (ret < 0) { close(); return ret; }

  m_workFrame = av_frame_alloc();
  if (!m_workFrame) { close(); return AVERROR(ENOMEM); }

  return 0;
}

// ── sendPacket ──────────────────────────────────────────────────────────

int AudioDecoder::sendPacket(AVPacket* pkt) {
  if (!m_codecCtx) return -EBADF;
  return avcodec_send_packet(m_codecCtx, pkt);
}

// ── recvFrame ───────────────────────────────────────────────────────────

int AudioDecoder::recvFrame(AVFrame* frame) {
  if (!m_codecCtx || !frame) return -EINVAL;

  int ret = avcodec_receive_frame(m_codecCtx, m_workFrame);
  if (ret < 0) return ret;

  av_frame_unref(frame);
  av_frame_move_ref(frame, m_workFrame);
  return 0;
}

// ── flush / close ────────────────────────────────────────────────────────

void AudioDecoder::flush() {
  if (m_codecCtx) {
    avcodec_flush_buffers(m_codecCtx);
  }
}

void AudioDecoder::close() {
  av_frame_free(&m_workFrame);
  if (m_codecCtx) {
    avcodec_free_context(&m_codecCtx);
    m_codecCtx = nullptr;
  }
}

} // namespace player
