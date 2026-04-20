#include <iostream>
#include <string>

#include "media_core/config/transcode_config.h"
#include "media_core/pipeline/transcode_pipeline.h"

int main(int argc, char **argv) {


    media_core::VideoTranscodeConfig cfg;
    cfg.input_path = "/home/bfm01000/workspace/source/cat.mp4";
    cfg.output_path = "/home/bfm01000/workspace/source/transCode.mp4";
    cfg.output_format = "mp4";
#ifdef __APPLE__
    cfg.video_encoder_name = "h264_videotoolbox";
    cfg.preferred_hw_device = "videotoolbox";
#else
    cfg.video_encoder_name = "mpeg4";
#endif
    cfg.enable_hw_decode = false;
    cfg.target_fps = 30;

    media_core::TranscodePipeline pipeline;
    media_core::Status st = pipeline.Run(cfg);
    if (!st.ok()) {
        std::cerr << "Transcode failed: " << st.message << " (ff_err=" << st.ffmpeg_error << ")\n";
        return 2;
    }

    std::cout << "Transcode succeeded: " << cfg.output_path << "\n";
    return 0;
}
