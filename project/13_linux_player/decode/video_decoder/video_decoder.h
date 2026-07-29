#pragma once

/// @file video_decoder.h
/// @brief 视频解码器。软件解码, 基于 FFmpeg avcodec send/receive 模型.
///
/// Thread safety:  由 Decode Thread 独占使用, 非线程安全.
///
/// Lifecycle:      open(codec_params) → sendPacket/recvFrame 循环 → flush/close
///
/// ==========================================================================
/// FFmpeg Send/Receive 模型
/// ==========================================================================
///   sendPacket(pkt)  → avcodec_send_packet()  // 喂入压缩数据
///   recvFrame(frame)  → avcodec_receive_frame() // 取出解码帧
///
///   send 和 recv 不是 1:1 的 — 可能 send 多次后才 recv 到一帧（B 帧重排），
///   也可能 send 一次 recv 多帧。调用者用 AVERROR(EAGAIN) 判断状态.

#include "decode/i_decoder.h"

struct AVCodecContext;
struct AVFrame;

namespace player {

class VideoDecoder : public IDecoder {
public:
  VideoDecoder();
  ~VideoDecoder() override;

  // ── IDecoder ──────────────────────────────────────────────────────────

  int  open(AVCodecParameters* codecParams)    override;
  int  sendPacket(AVPacket* pkt)               override;
  int  recvFrame(AVFrame* frame)               override;
  void flush()                                 override;
  void close()                                 override;

  /// 获取解码器上下文（调试/查询用）
  AVCodecContext* codecContext() const { return m_codecCtx; }

  /// 查询解码器是否已打开
  bool isOpen() const { return m_codecCtx != nullptr; }

private:
  AVCodecContext* m_codecCtx  = nullptr;
  AVFrame*        m_workFrame = nullptr;   // 复用的工作帧
};

} // namespace player
