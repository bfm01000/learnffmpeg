#pragma once

#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace player {

/// @brief Convert an FFmpeg error code to a human-readable string.
/// @param err  Negative FFmpeg error code (e.g. AVERROR(EAGAIN)).
/// @return String description of the error.
std::string getFFmpegErrorString(int err);

/// @brief Create, configure and open an AVCodecContext from codec parameters.
///
/// Steps performed:
///   - Find decoder via avcodec_find_decoder()
///   - Allocate context via avcodec_alloc_context3()
///   - Copy parameters via avcodec_parameters_to_context()
///   - Open decoder via avcodec_open2()
///
/// @param codec_params  Stream codec parameters to configure from.
/// @return Initialised AVCodecContext (caller must free), or nullptr on failure.
AVCodecContext* createCodecContext(AVCodecParameters* codec_params);

/// @brief Get the decoder short name for a given AVCodecID.
/// @param codec_id  The FFmpeg codec identifier.
/// @return Short name (e.g. "h264", "aac"), or "unknown".
std::string getDecoderName(AVCodecID codec_id);

} // namespace player
