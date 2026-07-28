#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace player {

/// @brief Abstract decoder interface — the base contract for all decoders.
///
/// All decoders (video, audio, subtitle) implement this interface.
/// Lifecycle: open -> sendPacket/recvFrame (repeated) -> flush -> close.
class IDecoder {
public:
    virtual ~IDecoder() = default;

    /// @brief Open the decoder with the given stream codec parameters.
    /// @param codec_params  Describes the codec, extradata, etc.
    /// @return 0 on success, negative FFmpeg error code on failure.
    virtual int open(AVCodecParameters* codec_params) = 0;

    /// @brief Send a compressed packet to the decoder.
    /// @param pkt  The AVPacket to decode (may be nullptr to signal drain).
    /// @return 0 on success, AVERROR(EAGAIN) if decoder is full,
    ///         or another negative error code.
    virtual int sendPacket(AVPacket* pkt) = 0;

    /// @brief Receive one decoded frame from the decoder.
    /// @param frame  Pre-allocated AVFrame that will be filled on success.
    /// @return 0 on success, AVERROR(EAGAIN) if more packets are needed,
    ///         AVERROR_EOF when drained, or another negative error code.
    virtual int recvFrame(AVFrame* frame) = 0;

    /// @brief Flush internal decoder buffers (seek or discontinuity).
    virtual void flush() = 0;

    /// @brief Close the decoder and release all resources.
    virtual void close() = 0;
};

} // namespace player
