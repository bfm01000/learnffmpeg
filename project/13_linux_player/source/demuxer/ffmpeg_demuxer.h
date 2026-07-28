#pragma once

/// @file ffmpeg_demuxer.h
/// @brief FFmpeg 解封装器。IMediaSource 的核心实现。
///
/// Thread safety:  非线程安全。由 Demux Thread 独占使用。
///
/// Ownership:      PlayerController 持有本实例。AVFormatContext 生命周期由本类管理。
///
/// Lifecycle:      open(url) → readPacket() 循环 → seekTo() 可选 → close()

#include "source/demuxer/i_media_source.h"
#include "source/demuxer/stream_info.h"
#include "source/protocol/i_protocol_handler.h"

#include <memory>
#include <string>
#include <vector>

struct AVFormatContext;

namespace player {

class FFmpegDemuxer : public IMediaSource {
public:
  FFmpegDemuxer();
  ~FFmpegDemuxer() override;

  // ── IMediaSource ──────────────────────────────────────────────────────

  int    open(const char* url)                  override;
  std::shared_ptr<AVPacket> readPacket()        override;
  int    seekTo(int64_t posMs)                  override;
  std::vector<StreamInfo> getStreams() const    override;
  int    close()                                override;

  /// 获取底层 AVFormatContext（调试/高级用途）
  AVFormatContext* formatContext() const { return m_fmtCtx; }

  /// 流数量
  int streamCount() const { return static_cast<int>(m_streamInfos.size()); }

private:
  void buildStreamInfos_();

  AVFormatContext*                     m_fmtCtx = nullptr;
  std::vector<StreamInfo>              m_streamInfos;
};

} // namespace player
