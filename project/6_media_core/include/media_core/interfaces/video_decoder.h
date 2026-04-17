#pragma once

#include "media_core/common/status.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace media_core {

class IVideoDecoder {
public:
    virtual ~IVideoDecoder() = default;

    virtual Status Open(int codec_id,
                        const AVCodecParameters *codecpar,
                        bool enable_hw_decode,
                        const char *preferred_hw_device) = 0;
    virtual AVCodecContext *Context() const = 0;
    virtual Status SendPacket(AVPacket *pkt) = 0;
    virtual Status SendEof() = 0;
    virtual Status ReceiveFrame(AVFrame *frame, bool *again, bool *eof) = 0;
    virtual Status Flush() = 0;
};

}  // namespace media_core
