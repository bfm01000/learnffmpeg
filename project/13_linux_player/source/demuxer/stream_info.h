#pragma once

/// @file stream_info.h
/// @brief 流元信息结构。由 FFmpegDemuxer 填充, 供上层查询.

#include "api/player_types.h"

#include <cstdint>
#include <string>

namespace player {

struct StreamInfo {
  int         index       = -1;
  MediaType   type        = MediaType::Unknown;
  int         codecId     = 0;       // AVCodecID
  std::string codecName;
  int         width       = 0;       // 视频专用
  int         height      = 0;       // 视频专用
  int         sampleRate  = 0;       // 音频专用
  int         channels    = 0;       // 音频专用
  int64_t     bitrate     = 0;
  int64_t     duration    = 0;       // us, AV_NOPTS_VALUE if unknown
};

/// AVMediaType → MediaType 映射
MediaType fromAVMediaType(int avType);

} // namespace player
