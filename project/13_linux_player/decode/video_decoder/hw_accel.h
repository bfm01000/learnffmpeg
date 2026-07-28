#pragma once

#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
}

#include "api/player_types.h"

namespace player {

/// @brief Abstract interface for hardware-accelerated video decoding backends.
///
/// Each concrete backend (VAAPI, VDPAU, CUDA) implements this interface.
/// The VideoDecoder selects and owns one IHWAccel instance when HW decoding
/// is requested and available.
class IHWAccel {
public:
    virtual ~IHWAccel() = default;

    /// @brief Return the HW acceleration backend type.
    virtual HWAccelBackend getType() const = 0;

    /// @brief Initialise the HW device context and attach it to the decoder.
    /// @param decoder_ctx  The AVCodecContext to attach hw_device_ctx to.
    /// @return 0 on success, negative FFmpeg error code on failure.
    virtual int init(AVCodecContext* decoder_ctx) = 0;

    /// @brief Return the pixel format that the HW decoder should use.
    virtual AVPixelFormat getFormat() const = 0;

    /// @brief Transfer a decoded HW frame to a software-readable AVFrame.
    /// @param hw_frame  The AVFrame received from avcodec_receive_frame
    ///                  (with HW pixel format).
    /// @return A new AVFrame in software format (caller must av_frame_free),
    ///         or nullptr on failure.
    virtual AVFrame* getFrame(AVFrame* hw_frame) = 0;

    /// @brief Factory: create an IHWAccel instance for the given backend.
    /// @param backend  Desired backend (None returns nullptr).
    /// @return Unique pointer to the backend, or nullptr if unsupported.
    static std::unique_ptr<IHWAccel> create(HWAccelBackend backend);
};

} // namespace player
