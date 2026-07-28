#include "video_decoder/hw_accel.h"

#include <cstdio>

extern "C" {
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
}

namespace player {

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<IHWAccel> IHWAccel::create(HWAccelBackend backend) {
    // TODO:
    //   Build a list of backends to try based on `backend`:
    //
    //     case HWAccelBackend::VAAPI:
    //       #if PLAYER_HWACCEL_VAAPI
    //         return std::make_unique<VAAPIAccel>();
    //       #else
    //         return nullptr;
    //       #endif
    //
    //     case HWAccelBackend::VDPAU:
    //       #if PLAYER_HWACCEL_VDPAU
    //         return std::make_unique<VDPAUAccel>();
    //       #else
    //         return nullptr;
    //       #endif
    //
    //     case HWAccelBackend::CUDA:
    //       #if PLAYER_HWACCEL_CUDA
    //         return std::make_unique<CUDAAccel>();
    //       #else
    //         return nullptr;
    //       #endif
    //
    //     case HWAccelBackend::Auto:
    //       Try each available backend in priority order (VAAPI > VDPAU > CUDA),
    //       returning the first one that succeeds (or nullptr if none work).
    //
    //     case HWAccelBackend::None:
    //     default:
    //       return nullptr;

    (void)backend;
    return nullptr;
}

// ============================================================================
// VAAPIAccel
// ============================================================================

#if PLAYER_HWACCEL_VAAPI

class VAAPIAccel : public IHWAccel {
public:
    HWAccelBackend getType() const override { return HWAccelBackend::VAAPI; }

    int init(AVCodecContext* decoder_ctx) override {
        // TODO:
        //   1. Create a VAAPI hardware device context:
        //        AVBufferRef* hw_dev = nullptr;
        //        int ret = av_hwdevice_ctx_create(
        //            &hw_dev, AV_HWDEVICE_TYPE_VAAPI, nullptr, nullptr, 0);
        //        if (ret < 0) return ret;
        //
        //   2. Attach the device context to the decoder:
        //        decoder_ctx->hw_device_ctx = av_buffer_ref(hw_dev);
        //
        //   3. Store the buffer ref and the HW pixel format:
        //        hw_device_ctx_ = hw_dev;
        //        hw_pix_fmt_ = AV_PIX_FMT_VAAPI;
        //
        //   4. Override get_format on decoder_ctx so it selects VAAPI:
        //        decoder_ctx->get_format = getVAAPIPixelFormat;
        //
        //      The callback inspects the available list and returns
        //      AV_PIX_FMT_VAAPI when present, falling back to the first
        //      software format otherwise.
        //
        //   5. Return 0 on success.
        //
        (void)decoder_ctx;
        return AVERROR(ENOSYS);
    }

    AVPixelFormat getFormat() const override {
        // return AV_PIX_FMT_VAAPI;
        return AV_PIX_FMT_NONE;
    }

    AVFrame* getFrame(AVFrame* hw_frame) override {
        // TODO:
        //   1. Allocate a software AVFrame:
        //        AVFrame* sw_frame = av_frame_alloc();
        //
        //   2. Download HW surface to system memory:
        //        int ret = av_hwframe_transfer_data(sw_frame, hw_frame, 0);
        //        if (ret < 0) { av_frame_free(&sw_frame); return nullptr; }
        //
        //   3. Copy frame metadata (pts, pict_type, sample_aspect_ratio, …):
        //        av_frame_copy_props(sw_frame, hw_frame);
        //
        //   4. Return sw_frame.
        //
        (void)hw_frame;
        return nullptr;
    }

private:
    AVBufferRef* hw_device_ctx_ = nullptr;
    AVPixelFormat hw_pix_fmt_ = AV_PIX_FMT_NONE;
};

#endif // PLAYER_HWACCEL_VAAPI

} // namespace player
