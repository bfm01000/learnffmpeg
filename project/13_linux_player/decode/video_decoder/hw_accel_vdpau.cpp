#include "video_decoder/hw_accel.h"

#include <cstdio>

extern "C" {
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
}

namespace player {

// ============================================================================
// VDPAUAccel  (stub — TODO implement)
// ============================================================================

#if PLAYER_HWACCEL_VDPAU

class VDPAUAccel : public IHWAccel {
public:
    HWAccelBackend getType() const override { return HWAccelBackend::VDPAU; }

    int init(AVCodecContext* decoder_ctx) override {
        // TODO: Implement VDPAU hardware decode support.
        //
        //   Steps:
        //     1. Open X11 display connection (required by VDPAU).
        //     2. Create HW device:
        //          av_hwdevice_ctx_create(&hw_dev,
        //                                  AV_HWDEVICE_TYPE_VDPAU,
        //                                  nullptr, nullptr, 0);
        //     3. Attach to decoder:
        //          decoder_ctx->hw_device_ctx = av_buffer_ref(hw_dev);
        //     4. Override get_format for AV_PIX_FMT_VDPAU.
        //     5. Handle VDPAU device initialisation failures gracefully.
        //
        (void)decoder_ctx;
        return AVERROR(ENOSYS);
    }

    AVPixelFormat getFormat() const override {
        // return AV_PIX_FMT_VDPAU;
        return AV_PIX_FMT_NONE;
    }

    AVFrame* getFrame(AVFrame* hw_frame) override {
        // TODO:
        //   Use av_hwframe_transfer_data() to download VDPAU surfaces
        //   to system memory. Similar to VAAPI implementation.
        //
        (void)hw_frame;
        return nullptr;
    }
};

#endif // PLAYER_HWACCEL_VDPAU

} // namespace player
