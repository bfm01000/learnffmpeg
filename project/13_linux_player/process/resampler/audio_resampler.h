#pragma once

#include <memory>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace player {

class AudioResampler {
public:
    AudioResampler();
    ~AudioResampler();

    AudioResampler(const AudioResampler&) = delete;
    AudioResampler& operator=(const AudioResampler&) = delete;
    AudioResampler(AudioResampler&&) = delete;
    AudioResampler& operator=(AudioResampler&&) = delete;

    /// @brief Initialize the resampler with input and output audio parameters.
    /// @return 0 on success, negative AVERROR on failure.
    int init(int in_sample_rate, AVSampleFormat in_fmt, int in_channels,
             int out_sample_rate, AVSampleFormat out_fmt, int out_channels);

    /// @brief Convert a single audio frame.
    /// @param in_frame  Input AVFrame (planar or interleaved).
    /// @return A newly allocated AVFrame with resampled data, or nullptr on error.
    AVFrame* convert(AVFrame* in_frame);

    /// @brief Drain any remaining buffered samples from the resampler.
    /// @return A frame with remaining samples, or nullptr when fully flushed.
    AVFrame* flush();

    /// @brief Close and release the resampler context.
    void close();

private:
    SwrContext* swr_ctx_{nullptr};
};

} // namespace player
