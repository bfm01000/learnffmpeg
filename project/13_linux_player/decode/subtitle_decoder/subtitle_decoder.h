#pragma once

#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

#include "decode/i_decoder.h"
#include "core/queue/frame_queue.h"

namespace player {

/// @brief Subtitle decoder handling both text and bitmap subtitles.
///
/// Supports bitmap subtitles (DVD/Blu-ray VOBSUB, DVB) and text subtitles
/// (SRT, ASS/SSA, UTF-8 plaintext).  Uses the FFmpeg subtitle decoding API.
///
/// Decoded subtitles (represented as AVFrame with subtitle data or as
/// AVSubtitle structs) are pushed to the FrameQueue for the subtitle
/// renderer pipeline.
class SubtitleDecoder : public IDecoder {
public:
    explicit SubtitleDecoder(std::shared_ptr<FrameQueue<AVFrame*>> frame_queue);
    ~SubtitleDecoder() override;

    // -- IDecoder interface ------------------------------------------------
    int open(AVCodecParameters* codec_params) override;
    int sendPacket(AVPacket* pkt) override;
    int recvFrame(AVFrame* frame) override;
    void flush() override;
    void close() override;

private:
    /// Convert an FFmpeg AVSubtitle into our internal AVFrame representation.
    /// This handles both text subtitles and bitmap subtitle rectangles.
    int subtitleToFrame(AVSubtitle* sub, AVFrame* frame);

    AVCodecContext* codec_ctx_ = nullptr;
    AVFrame* frame_ = nullptr;
    std::shared_ptr<FrameQueue<AVFrame*>> frame_queue_;

    // Buffered subtitle state (for bitmap subtitles requiring composite)
    AVSubtitle current_sub_;
    bool has_pending_sub_ = false;
};

} // namespace player
