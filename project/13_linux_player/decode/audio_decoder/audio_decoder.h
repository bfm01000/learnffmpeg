#pragma once

#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
}

#include "decode/i_decoder.h"
#include "core/queue/frame_queue.h"

namespace player {

/// @brief Audio decoder that auto-resamples to a target format.
///
/// Decodes compressed audio packets (AAC, MP3, Opus, etc.) and resamples
/// the output PCM to a configurable target sample format, sample rate, and
/// channel layout.  Decoded + resampled frames are pushed to the shared
/// FrameQueue for consumption by the audio renderer.
class AudioDecoder : public IDecoder {
public:
    /// @param frame_queue     Output queue for decoded/resampled frames.
    /// @param target_rate     Target sample rate (Hz). 0 = use input rate.
    /// @param target_fmt      Target sample format. AV_SAMPLE_FMT_NONE = input fmt.
    /// @param target_channels Target channel layout. 0 = input layout.
    explicit AudioDecoder(std::shared_ptr<FrameQueue<AVFrame*>> frame_queue,
                          int target_rate = 48000,
                          AVSampleFormat target_fmt = AV_SAMPLE_FMT_FLT,
                          uint64_t target_ch_layout = AV_CH_LAYOUT_STEREO);

    ~AudioDecoder() override;

    // -- IDecoder interface ------------------------------------------------
    int open(AVCodecParameters* codec_params) override;
    int sendPacket(AVPacket* pkt) override;
    int recvFrame(AVFrame* frame) override;
    void flush() override;
    void close() override;

private:
    /// (Re-)create the SwrContext for the current decoder output parameters.
    int setupResampler(AVCodecContext* ctx);

    AVCodecContext* codec_ctx_ = nullptr;
    AVFrame* frame_ = nullptr;                         // reusable internal frame
    std::shared_ptr<FrameQueue<AVFrame*>> frame_queue_;

    SwrContext* swr_ctx_ = nullptr;

    // Target audio parameters
    int target_sample_rate_;
    AVSampleFormat target_sample_fmt_;
    uint64_t target_ch_layout_;
};

} // namespace player
