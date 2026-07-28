#pragma once

#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

#include "decode/i_decoder.h"
#include "decode/video_decoder/hw_accel.h"
#include "core/queue/frame_queue.h"

namespace player {

/// @brief Software + hardware video decoder.
///
/// Decodes H.264 / H.265 / VP9 / AV1 (etc.) video streams.
/// Supports both pure software decoding and hardware-accelerated decoding
/// via VAAPI, VDPAU, or CUDA backends.
///
/// On each successful recvFrame(), the decoded frame is also pushed to the
/// shared FrameQueue for consumption by the video renderer pipeline.
class VideoDecoder : public IDecoder {
public:
    /// @param frame_queue  Output queue where decoded frames are pushed.
    explicit VideoDecoder(std::shared_ptr<FrameQueue<AVFrame*>> frame_queue);

    ~VideoDecoder() override;

    // -- IDecoder interface ------------------------------------------------
    int open(AVCodecParameters* codec_params) override;
    int sendPacket(AVPacket* pkt) override;
    int recvFrame(AVFrame* frame) override;
    void flush() override;
    void close() override;

private:
    /// Try to initialise HW acceleration for the given codec context.
    /// Falls back gracefully: returns false without producing an error.
    bool tryInitHWAccel(AVCodecContext* ctx, HWAccelBackend backend);

    AVCodecContext* codec_ctx_ = nullptr;
    AVFrame* frame_ = nullptr;                         // reusable internal frame
    std::shared_ptr<FrameQueue<AVFrame*>> frame_queue_;

    HWAccelBackend hw_backend_ = HWAccelBackend::None;
    std::unique_ptr<IHWAccel> hw_accel_;
    bool is_hw_ = false;
};

} // namespace player
