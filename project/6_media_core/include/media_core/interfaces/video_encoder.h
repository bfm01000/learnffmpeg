#pragma once

#include "media_core/common/status.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace media_core {

class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;

    virtual Status Open(const char *encoder_name,
                        int width,
                        int height,
                        AVRational time_base,
                        AVRational frame_rate,
                        int bitrate) = 0;
    virtual AVCodecContext *Context() const = 0;
    virtual Status SendFrame(AVFrame *frame) = 0;
    virtual Status SendEof() = 0;
    virtual Status ReceivePacket(AVPacket *pkt, bool *again, bool *eof) = 0;
};

}  // namespace media_core
