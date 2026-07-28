#include "subtitle_decoder.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
}

namespace player {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

SubtitleDecoder::SubtitleDecoder(
    std::shared_ptr<FrameQueue<AVFrame*>> frame_queue)
    : frame_queue_(std::move(frame_queue)) {
    std::memset(&current_sub_, 0, sizeof(current_sub_));
}

SubtitleDecoder::~SubtitleDecoder() {
    close();
}

// ---------------------------------------------------------------------------
// open
// ---------------------------------------------------------------------------
int SubtitleDecoder::open(AVCodecParameters* codec_params) {
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
    //   5. Initialise subtitle decoding state.
    //
    //   6. Log decoder info (codec name, subtitle type).

    (void)codec_params;
    return -1; // TODO: implement
}

// ---------------------------------------------------------------------------
// sendPacket
// ---------------------------------------------------------------------------
int SubtitleDecoder::sendPacket(AVPacket* pkt) {
    // TODO:
    //   Unlike video/audio, FFmpeg subtitle decoding traditionally uses
    //   avcodec_decode_subtitle2().  Wrap it to conform to IDecoder:
    //
    //   If pkt is nullptr (drain signal):
    //     - Process any pending subtitle data.
    //     - Return AVERROR_EOF.
    //
    //   Otherwise:
    //     int got_sub = 0;
    //     AVSubtitle sub;
    //     int len = avcodec_decode_subtitle2(codec_ctx_, &sub,
    //                                         &got_sub, pkt);
    //     if (len < 0) return len;
    //     if (got_sub) {
    //         Store the decoded subtitle for recvFrame() to retrieve.
    //         has_pending_sub_ = true;
    //         current_sub_ = sub;
    //     }
    //     return 0;

    (void)pkt;
    return -1; // TODO: implement
}

// ---------------------------------------------------------------------------
// recvFrame
// ---------------------------------------------------------------------------
int SubtitleDecoder::recvFrame(AVFrame* frame) {
    // TODO:
    //   1. If has_pending_sub_ is false, return EAGAIN.
    //
    //   2. Convert subtitle to AVFrame representation:
    //        subtitleToFrame(&current_sub_, frame);
    //
    //   3. Push clone to frame_queue_:
    //        AVFrame* qf = av_frame_clone(frame);
    //        frame_queue_->pushFrame(qf);
    //
    //   4. Clean up the AVSubtitle:
    //        avsubtitle_free(&current_sub_);
    //        has_pending_sub_ = false;
    //
    //   5. Return 0.

    (void)frame;
    return -1; // TODO: implement
}

// ---------------------------------------------------------------------------
// flush
// ---------------------------------------------------------------------------
void SubtitleDecoder::flush() {
    // TODO:
    //   1. avcodec_flush_buffers(codec_ctx_);
    //   2. If has_pending_sub_:
    //        avsubtitle_free(&current_sub_);
    //        has_pending_sub_ = false;
    //   3. frame_queue_->flush();

    // TODO: implement
}

// ---------------------------------------------------------------------------
// close
// ---------------------------------------------------------------------------
void SubtitleDecoder::close() {
    // TODO:
    //   1. flush();
    //   2. av_frame_free(&frame_);
    //   3. avcodec_free_context(&codec_ctx_);
    //   4. frame_queue_->flush();

    // TODO: implement
}

// ---------------------------------------------------------------------------
// subtitleToFrame  (private helper)
// ---------------------------------------------------------------------------
int SubtitleDecoder::subtitleToFrame(AVSubtitle* sub, AVFrame* frame) {
    // TODO: Convert AVSubtitle to AVFrame.
    //
    //   For text subtitles (sub->num_rects == 0 with text fields, or
    //   ASS text):
    //     - Store the text as side data or in a custom AVFrame buffer.
    //     - Set frame metadata: pts, duration, type.
    //
    //   For bitmap subtitles (sub->num_rects > 0 with .pict data):
    //     - Composite all rectangles into a single RGBA frame.
    //     - Set frame dimensions, pixel format (AV_PIX_FMT_RGBA).
    //     - Store pts, duration.
    //
    //   Reference:
    //     frame->pts = sub->pts / AV_TIME_BASE;
    //     frame->pkt_duration = sub->end_display_time - sub->start_display_time;
    //
    //   Return 0 on success, negative error code on failure.

    (void)sub;
    (void)frame;
    return -1; // TODO: implement
}

} // namespace player
