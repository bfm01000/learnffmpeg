#include "frame.h"

extern "C" {
#include <libavutil/frame.h>
}

namespace player {

// Custom deleter for AVFrame
namespace {

void avFrameDeleter(AVFrame* f) {
    if (f) {
        av_frame_unref(f);
        av_frame_free(&f);
    }
}

} // anonymous namespace

Frame::Frame()
    : frame(nullptr)
    , pts(0.0)
    , duration(0.0)
    , pos(-1)
    , serial(0)
    , media_type(MediaType::Unknown)
    , width(0)
    , height(0)
    , sample_rate(0)
    , channels(0)
{}

Frame::Frame(AVFrame* av_frame, MediaType type)
    : frame(av_frame ? AVFramePtr(av_frame, avFrameDeleter) : nullptr)
    , pts(0.0)
    , duration(0.0)
    , pos(-1)
    , serial(0)
    , media_type(type)
    , width(0)
    , height(0)
    , sample_rate(0)
    , channels(0)
{
    if (av_frame) {
        if (av_frame->pts != int64_t(AV_NOPTS_VALUE)) {
            pts = av_frame->pts * av_q2d(av_frame->time_base);
        }
        // Populate media-specific fields
        switch (type) {
            case MediaType::Video:
                width = av_frame->width;
                height = av_frame->height;
                break;
            case MediaType::Audio:
                sample_rate = av_frame->sample_rate;
                channels = av_frame->ch_layout.nb_channels;
                break;
            default:
                break;
        }
    }
}

Frame::Frame(AVFramePtr av_frame, MediaType type)
    : frame(std::move(av_frame))
    , pts(0.0)
    , duration(0.0)
    , pos(-1)
    , serial(0)
    , media_type(type)
    , width(0)
    , height(0)
    , sample_rate(0)
    , channels(0)
{
    auto* raw = this->frame.get();
    if (raw) {
        if (raw->pts != int64_t(AV_NOPTS_VALUE)) {
            pts = raw->pts * av_q2d(raw->time_base);
        }
        switch (type) {
            case MediaType::Video:
                width = raw->width;
                height = raw->height;
                break;
            case MediaType::Audio:
                sample_rate = raw->sample_rate;
                channels = raw->ch_layout.nb_channels;
                break;
            default:
                break;
        }
    }
}

void Frame::reset() {
    frame.reset();
    pts = 0.0;
    duration = 0.0;
    pos = -1;
    serial = 0;
    media_type = MediaType::Unknown;
    width = 0;
    height = 0;
    sample_rate = 0;
    channels = 0;
}

Frame Frame::cloneMeta() const {
    Frame meta;
    meta.pts = pts;
    meta.duration = duration;
    meta.pos = pos;
    meta.serial = serial;
    meta.media_type = media_type;
    meta.width = width;
    meta.height = height;
    meta.sample_rate = sample_rate;
    meta.channels = channels;
    return meta;
}

} // namespace player
