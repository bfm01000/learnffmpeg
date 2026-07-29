/// @file video_decoder.cpp
/// @brief VideoDecoder — 基于 FFmpeg avcodec 的软件视频解码器.

#include "decode/video_decoder/video_decoder.h"

#include <cerrno>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
}

namespace player {

// ── 生命周期 ────────────────────────────────────────────────────────────

VideoDecoder::VideoDecoder() = default;

VideoDecoder::~VideoDecoder() {
  close();
}

// ── open ─────────────────────────────────────────────────────────────────

int VideoDecoder::open(AVCodecParameters* codecParams) {
  if (!codecParams) return -EINVAL;

  // 确保之前的解码器已关闭
  close();

  // 查找解码器
  const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
  if (!codec) return AVERROR_DECODER_NOT_FOUND;

  // 分配解码器上下文
  m_codecCtx = avcodec_alloc_context3(codec);
  if (!m_codecCtx) return AVERROR(ENOMEM);

  // 拷贝编解码参数
  int ret = avcodec_parameters_to_context(m_codecCtx, codecParams);
  if (ret < 0) {
    close();
    return ret;
  }

  // 打开解码器（v1: 软件解码, 不传 options）
  ret = avcodec_open2(m_codecCtx, codec, nullptr);
  if (ret < 0) {
    close();
    return ret;
  }

  // 分配内部工作帧
  m_workFrame = av_frame_alloc();
  if (!m_workFrame) {
    close();
    return AVERROR(ENOMEM);
  }

  return 0;
}

// ── sendPacket ──────────────────────────────────────────────────────────

int VideoDecoder::sendPacket(AVPacket* pkt) {
  if (!m_codecCtx) return -EBADF;
  return avcodec_send_packet(m_codecCtx, pkt);
}

// ── recvFrame ───────────────────────────────────────────────────────────

int VideoDecoder::recvFrame(AVFrame* frame) {
  if (!m_codecCtx || !frame) return -EINVAL;

  int ret = avcodec_receive_frame(m_codecCtx, m_workFrame);
  if (ret < 0) return ret;

  // 将解码器内部帧的数据移动到调用者提供的帧
  av_frame_unref(frame);
  av_frame_move_ref(frame, m_workFrame);

  return 0;
}

// ── flush ───────────────────────────────────────────────────────────────

void VideoDecoder::flush() {
  if (m_codecCtx) {
    avcodec_flush_buffers(m_codecCtx);
  }
}

// ── close ────────────────────────────────────────────────────────────────

void VideoDecoder::close() {
  av_frame_free(&m_workFrame);
  if (m_codecCtx) {
    avcodec_free_context(&m_codecCtx);
    m_codecCtx = nullptr;
  }
}

} // namespace player
