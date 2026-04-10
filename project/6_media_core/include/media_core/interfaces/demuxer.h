#pragma once

#include "media_core/common/status.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
}

namespace media_core {

class IDemuxer {
public:
    virtual ~IDemuxer() = default;

    virtual Status Open(const char *input_path) = 0;
    virtual int VideoStreamIndex() const = 0;
    virtual Status ReadPacket(AVPacket *pkt, bool *eof) = 0;
    virtual AVRational VideoStreamTimeBase() const = 0;
    virtual int VideoCodecId() const = 0;
    virtual const AVCodecParameters *VideoCodecParameters() const = 0;
    virtual int VideoWidth() const = 0;
    virtual int VideoHeight() const = 0;
    virtual AVRational VideoFrameRate() const = 0;

    virtual bool HasAudioStream() const = 0;
    virtual int AudioStreamIndex() const = 0;
    virtual AVRational AudioStreamTimeBase() const = 0;
    virtual const AVCodecParameters *AudioCodecParameters() const = 0;
};

}  // namespace media_core
