#include "decode_utils.h"

#include <cstring>

extern "C" {
#include <libavutil/error.h>
}

namespace player {

// ---------------------------------------------------------------------------
// getFFmpegErrorString
// ---------------------------------------------------------------------------
std::string getFFmpegErrorString(int err) {
    // TODO:
    //   Use av_strerror() to safely convert the negative error code into
    //   a human-readable string (up to AV_ERROR_MAX_STRING_SIZE bytes).
    //
    //   Example:
    //     char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    //     av_strerror(err, buf, sizeof(buf));
    //     return std::string(buf);
    //
    //   For unknown / unhandled codes, fall back to a numeric representation
    //   like "Unknown error (-nn)".

    (void)err;
    return {};
}

// ---------------------------------------------------------------------------
// createCodecContext
// ---------------------------------------------------------------------------
AVCodecContext* createCodecContext(AVCodecParameters* codec_params) {
    // TODO:
    //   1. Guard: return nullptr if codec_params is null.
    //
    //   2. Find decoder:
    //        const AVCodec* decoder =
    //            avcodec_find_decoder(codec_params->codec_id);
    //      If not found, print error and return nullptr.
    //
    //   3. Allocate context:
    //        AVCodecContext* ctx = avcodec_alloc_context3(decoder);
    //      Guard allocation failure.
    //
    //   4. Copy codec parameters into the context:
    //        int ret = avcodec_parameters_to_context(ctx, codec_params);
    //      On failure, free context and return nullptr.
    //
    //   5. Open the decoder:
    //        ret = avcodec_open2(ctx, decoder, nullptr);
    //      On failure, free context and return nullptr.
    //
    //   6. Return ctx.
    //
    //   Note: For hardware-accelerated decoding, the caller (VideoDecoder)
    //   will attach an hw_device_ctx after this function returns.

    (void)codec_params;
    return nullptr;
}

// ---------------------------------------------------------------------------
// getDecoderName
// ---------------------------------------------------------------------------
std::string getDecoderName(AVCodecID codec_id) {
    // TODO:
    //   const AVCodec* decoder = avcodec_find_decoder(codec_id);
    //   if (decoder) return std::string(decoder->name);
    //   return "unknown";

    (void)codec_id;
    return "unknown";
}

} // namespace player
