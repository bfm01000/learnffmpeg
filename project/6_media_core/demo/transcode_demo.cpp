#include <iostream>
#include <string>

#include "media_core/config/transcode_config.h"
#include "media_core/pipeline/transcode_pipeline.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "Usage: media_core_demo <input> <output> [encoder_name]\n";
        return 1;
    }

    media_core::VideoTranscodeConfig cfg;
    cfg.input_path = argv[1];
    cfg.output_path = argv[2];
    cfg.output_format = "mp4";
    cfg.video_encoder_name = (argc >= 4) ? argv[3] : "libx264";
    cfg.enable_hw_decode = true;
    cfg.preferred_hw_device = "videotoolbox";
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
