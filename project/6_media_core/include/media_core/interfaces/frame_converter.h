#pragma once

#include "media_core/common/status.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace media_core {

class IFrameConverter {
public:
    virtual ~IFrameConverter() = default;

    virtual Status Init(AVCodecContext *in_ctx, int out_w, int out_h, AVPixelFormat out_fmt) = 0;
    virtual Status Convert(const AVFrame *in_frame, AVFrame *out_frame) = 0;
};

}  // namespace media_core
