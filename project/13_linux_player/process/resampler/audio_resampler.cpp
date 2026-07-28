#include "audio_resampler.h"

namespace player {

AudioResampler::AudioResampler() = default;

AudioResampler::~AudioResampler() {
    close();
}

int AudioResampler::init(int in_sample_rate, AVSampleFormat in_fmt, int in_channels,
                          int out_sample_rate, AVSampleFormat out_fmt, int out_channels) {
    // TODO: Allocate SwrContext with swr_alloc_set_opts(),
    //       open it with swr_init(), store in swr_ctx_.
    return 0;
}

AVFrame* AudioResampler::convert(AVFrame* in_frame) {
    // TODO: Allocate output AVFrame, call swr_convert(),
    //       return the converted frame.
    return nullptr;
}

AVFrame* AudioResampler::flush() {
    // TODO: Call swr_convert() with nullptr input to drain samples,
    //       return remaining frames until no more data.
    return nullptr;
}

void AudioResampler::close() {
    // TODO: Free swr_ctx_ with swr_free(), nullify pointer.
}

} // namespace player
