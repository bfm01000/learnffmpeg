#pragma once

#include "media_core/common/status.h"
#include "media_core/config/transcode_config.h"

namespace media_core {

class TranscodePipeline {
public:
    Status Run(const VideoTranscodeConfig &config);
};

}  // namespace media_core
