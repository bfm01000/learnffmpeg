#include "video_decoder/hw_accel.h"

#include <cstdio>

extern "C" {
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
}

namespace player {

// ============================================================================
// CUDAAccel  (stub — TODO implement)
// ============================================================================

#if PLAYER_HWACCEL_CUDA

class CUDAAccel : public IHWAccel {
public:
    HWAccelBackend getType() const override { return HWAccelBackend::CUDA; }

    int init(AVCodecContext* decoder_ctx) override {
        // TODO: Implement CUDA hardware decode support.
        //
        //   Steps:
        //     1. Create CUDA HW device:
        //          av_hwdevice_ctx_create(&hw_dev,
        //                                  AV_HWDEVICE_TYPE_CUDA,
        //                                  nullptr, nullptr, 0);
        //     2. Attach to decoder:
        //          decoder_ctx->hw_device_ctx = av_buffer_ref(hw_dev);
        //     3. Override get_format for AV_PIX_FMT_CUDA.
        //
        //   Prerequisites:
        //     - NVIDIA GPU with NVDEC/NVENCO support.
        //     - CUDA toolkit and driver installed.
        //     - FFmpeg compiled with --enable-cuda-nvcc --enable-nonfree
        //       --enable-libnpp (if scaling is needed).
        //
        (void)decoder_ctx;
        return AVERROR(ENOSYS);
    }

    AVPixelFormat getFormat() const override {
        // return AV_PIX_FMT_CUDA;
        return AV_PIX_FMT_NONE;
    }

    AVFrame* getFrame(AVFrame* hw_frame) override {
        // TODO:
        //   Use av_hwframe_transfer_data() to download CUDA device
        //   memory to system memory.
        //
        (void)hw_frame;
        return nullptr;
    }
};

#endif // PLAYER_HWACCEL_CUDA

} // namespace player
