#include "video_decoder.h"

#include <cstdio>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

namespace player {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

VideoDecoder::VideoDecoder(std::shared_ptr<FrameQueue<AVFrame*>> frame_queue)
    : frame_queue_(std::move(frame_queue)) {
}

VideoDecoder::~VideoDecoder() {
    close();
}

// ---------------------------------------------------------------------------
// open
// ---------------------------------------------------------------------------
int VideoDecoder::open(AVCodecParameters* codec_params) {
    // TODO:
    //   1. If already open, close() first.
    //
    //   2. Create codec context via decode_utils::createCodecContext().
    //      Return error if it fails.
    //
    //   3. Allocate internal frame:
    //        frame_ = av_frame_alloc();
    //
    //   4. HW acceleration path:
    //      a. Query player config for desired HWAccelBackend.
    //      b. Call tryInitHWAccel() for the requested backend(s).
    //      c. If HW init succeeds, set is_hw_ = true.
    //      d. If HW init fails, log warning and fall through to software.
    //
    //   5. Open codec:
    //        ret = avcodec_open2(codec_ctx_, codec_ctx_->codec, nullptr);
    //
    //   6. Log decoder info (codec name, pixel format, HW status).

    (void)codec_params;
    return -1; // TODO: implement
}

// ---------------------------------------------------------------------------
// sendPacket
// ---------------------------------------------------------------------------
int VideoDecoder::sendPacket(AVPacket* pkt) {
    // TODO:
    //   return avcodec_send_packet(codec_ctx_, pkt);
    //
    //   Handle AVERROR(EAGAIN) — decoder is full, caller should recvFrame first.
    //   Handle AVERROR(ENOMEM) / other errors appropriately.

    (void)pkt;
    return -1; // TODO: implement
}

// ---------------------------------------------------------------------------
// recvFrame
// ---------------------------------------------------------------------------
int VideoDecoder::recvFrame(AVFrame* frame) {
    // TODO:
    //   1. ret = avcodec_receive_frame(codec_ctx_, frame_);
    //
    //   2. If ret != 0, return ret directly (EAGAIN / EOF / error).
    //
    //   3. If is_hw_:
    //        AVFrame* sw_frame = hw_accel_->getFrame(frame_);
    //        av_frame_move_ref(frame, sw_frame);
    //        av_frame_free(&sw_frame);
    //      Else:
    //        av_frame_move_ref(frame, frame_);
    //
    //   4. Push a clone of the frame to frame_queue_:
    //        AVFrame* queue_frame = av_frame_clone(frame);
    //        frame_queue_->pushFrame(queue_frame);
    //        (If push fails, free the clone so we don't leak.)
    //
    //   5. Return 0.

    (void)frame;
    return -1; // TODO: implement
}

// ---------------------------------------------------------------------------
// flush
// ---------------------------------------------------------------------------
void VideoDecoder::flush() {
    // TODO:
    //   1. avcodec_flush_buffers(codec_ctx_);
    //   2. frame_queue_->flush();
    //
    //   Note: After flush, sendPacket() must be called again before
    //   recvFrame() to feed the new GOP.

    // TODO: implement
}

// ---------------------------------------------------------------------------
// close
// ---------------------------------------------------------------------------
void VideoDecoder::close() {
    // TODO:
    //   1. hw_accel_.reset();
    //   2. av_frame_free(&frame_);
    //   3. avcodec_free_context(&codec_ctx_);
    //   4. frame_queue_->flush();

    // TODO: implement
}

// ---------------------------------------------------------------------------
// tryInitHWAccel
// ---------------------------------------------------------------------------
bool VideoDecoder::tryInitHWAccel(AVCodecContext* ctx, HWAccelBackend backend) {
    // TODO:
    //   1. If backend == None, return false.
    //
    //   2. Create IHWAccel instance:
    //        auto accel = IHWAccel::create(backend);
    //
    //   3. If !accel, return false.
    //
    //   4. Call accel->init(ctx).
    //      - On success: store accel in hw_accel_, set hw_backend_,
    //        is_hw_ = true, return true.
    //      - On failure: log warning, return false.
    //
    //   5. Also override the AVCodecContext get_format callback:
    //        ctx->get_format = [](AVCodecContext*,
    //                             const AVPixelFormat* fmt) -> AVPixelFormat {
    //          // Return hw_accel_->getFormat() from the list.
    //        };
    //      (C++ lambda can be used, but be careful with lifetime —
    //       a static-free approach is to store the pointer in ctx->opaque.)

    (void)ctx;
    (void)backend;
    return false;
}

} // namespace player
