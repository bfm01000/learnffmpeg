#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace media_core {

inline void FreePacket(AVPacket *pkt) { av_packet_free(&pkt); }
inline void FreeFrame(AVFrame *frame) { av_frame_free(&frame); }
inline void FreeCodecContext(AVCodecContext *ctx) { avcodec_free_context(&ctx); }
inline void FreeFormatInputContext(AVFormatContext *ctx) { avformat_close_input(&ctx); }
inline void FreeSwsContext(SwsContext *ctx) { sws_freeContext(ctx); }

}  // namespace media_core
