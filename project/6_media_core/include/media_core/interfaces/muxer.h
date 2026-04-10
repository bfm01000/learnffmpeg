#pragma once

#include "media_core/common/status.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace media_core {

class IMuxer {
public:
    virtual ~IMuxer() = default;

    virtual Status Open(const char *output_path, const char *output_format) = 0;
    virtual Status AddVideoStreamFromEncoder(const AVCodecContext *enc_ctx, int *out_stream_index) = 0;
    virtual Status AddAudioStreamFromCodecParameters(const AVCodecParameters *codecpar,
                                                     AVRational time_base,
                                                     int *out_stream_index) = 0;
    virtual Status WriteHeader() = 0;
    virtual Status WritePacket(AVPacket *pkt, AVRational src_tb, int out_stream_index) = 0;
    virtual Status WriteTrailer() = 0;
};

}  // namespace media_core
