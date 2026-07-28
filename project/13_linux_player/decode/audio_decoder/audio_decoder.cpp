#include "audio_decoder.h"

#include <cstdio>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace player {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

AudioDecoder::AudioDecoder(std::shared_ptr<FrameQueue<AVFrame*>> frame_queue,
                           int target_rate,
                           AVSampleFormat target_fmt,
                           uint64_t target_ch_layout)
    : frame_queue_(std::move(frame_queue))
    , target_sample_rate_(target_rate)
    , target_sample_fmt_(target_fmt)
    , target_ch_layout_(target_ch_layout) {
}

AudioDecoder::~AudioDecoder() {
    close();
}

// ---------------------------------------------------------------------------
// open
// ---------------------------------------------------------------------------
int AudioDecoder::open(AVCodecParameters* codec_params) {
    // TODO:
    //   1. If already open, close() first.
    //
    //   2. Create codec context via decode_utils::createCodecContext().
    //      Return error if it fails.
    //
    //   3. Allocate internal frame:
    //        frame_ = av_frame_alloc();
    //
    //   4. Open the codec:
    //        ret = avcodec_open2(codec_ctx_, codec_ctx_->codec, nullptr);
    //
    //   5. Set up the resampler (if target format differs from input):
    //        setupResampler(codec_ctx_);
    //
    //   6. Log decoder info (codec name, sample rate, channels, format).

    (void)codec_params;
    return -1; // TODO: implement
}

// ---------------------------------------------------------------------------
// sendPacket
// ---------------------------------------------------------------------------
int AudioDecoder::sendPacket(AVPacket* pkt) {
    // TODO:
    //   return avcodec_send_packet(codec_ctx_, pkt);
    //
    //   Handle EAGAIN / ENOMEM / other errors.

    (void)pkt;
    return -1; // TODO: implement
}

// ---------------------------------------------------------------------------
// recvFrame
// ---------------------------------------------------------------------------
int AudioDecoder::recvFrame(AVFrame* frame) {
    // TODO:
    //   1. ret = avcodec_receive_frame(codec_ctx_, frame_);
    //      If ret != 0, return ret directly.
    //
    //   2. If swr_ctx_ is valid, resample:
    //        AVFrame* resampled = av_frame_alloc();
    //        // Allocate resampled frame with target parameters
    //        resampled->sample_rate = target_sample_rate_;
    //        resampled->ch_layout = ...;
    //        resampled->format = target_sample_fmt_;
    //        av_frame_get_buffer(resampled, 0);
    //
    //        int nb_samples = swr_convert(swr_ctx_,
    //            resampled->data, resampled->nb_samples,
    //            (const uint8_t**)frame_->data, frame_->nb_samples);
    //        resampled->nb_samples = nb_samples;
    //        // Copy pts / duration
    //        av_frame_copy_props(resampled, frame_);
    //
    //        av_frame_move_ref(frame, resampled);
    //   3. Else:
    //        av_frame_move_ref(frame, frame_);
    //
    //   4. Push clone to frame_queue_:
    //        AVFrame* qf = av_frame_clone(frame);
    //        frame_queue_->pushFrame(qf);
    //
    //   5. Return 0.

    (void)frame;
    return -1; // TODO: implement
}

// ---------------------------------------------------------------------------
// flush
// ---------------------------------------------------------------------------
void AudioDecoder::flush() {
    // TODO:
    //   1. avcodec_flush_buffers(codec_ctx_);
    //   2. swr_close(swr_ctx_);  // discard any buffered samples in resampler
    //   3. frame_queue_->flush();

    // TODO: implement
}

// ---------------------------------------------------------------------------
// close
// ---------------------------------------------------------------------------
void AudioDecoder::close() {
    // TODO:
    //   1. swr_free(&swr_ctx_);
    //   2. av_frame_free(&frame_);
    //   3. avcodec_free_context(&codec_ctx_);
    //   4. frame_queue_->flush();

    // TODO: implement
}

// ---------------------------------------------------------------------------
// setupResampler
// ---------------------------------------------------------------------------
int AudioDecoder::setupResampler(AVCodecContext* ctx) {
    // TODO:
    //   1. Free any existing SwrContext:
    //        swr_free(&swr_ctx_);
    //
    //   2. If all target parameters match the decoder's output parameters,
    //      skip resampling entirely (set swr_ctx_ = nullptr and return 0).
    //
    //   3. Allocate SwrContext:
    //        swr_alloc_set_opts2(&swr_ctx_,
    //            &target_ch_layout_, target_sample_fmt_, target_sample_rate_,
    //            &ctx->ch_layout,   ctx->sample_fmt,   ctx->sample_rate,
    //            0, nullptr);
    //
    //   4. Initialise:
    //        swr_init(swr_ctx_);
    //
    //   5. Return 0 on success, negative FFmpeg error otherwise.

    (void)ctx;
    return -1; // TODO: implement
}

} // namespace player
