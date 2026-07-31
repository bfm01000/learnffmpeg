#pragma once

#include "api/player_types.h"

#include <cstdint>
#include <memory>

struct AVFrame;

namespace player {

struct Frame {
    using AVFramePtr = std::shared_ptr<AVFrame>;

    AVFramePtr frame;     ///< Shared AVFrame (reference counted)
    double pts;           ///< Presentation timestamp in seconds
    double duration;      ///< Frame duration in seconds
    int64_t pos;          ///< Byte position in stream
    int64_t serial;       ///< Decoder serial (incremented on seek)
    MediaType media_type; ///< Type of media this frame belongs to
    int width;            ///< Video width (valid for video)
    int height;           ///< Video height (valid for video)
    int sample_rate;      ///< Audio sample rate (valid for audio)
    int channels;         ///< Audio channels (valid for audio)

    Frame();

    // Create frame with shared AVFrame ownership
    explicit Frame(AVFrame* av_frame, MediaType type = MediaType::Unknown);

    // Create frame from an existing shared_ptr
    Frame(AVFramePtr av_frame, MediaType type = MediaType::Unknown);

    // Check if frame has valid data
    bool isValid() const { return frame != nullptr; }

    // Release the frame data
    void reset();

    // Clone metadata without copying AVFrame
    Frame cloneMeta() const;
};

} // namespace player
