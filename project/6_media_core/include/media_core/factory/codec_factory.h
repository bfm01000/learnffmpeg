#pragma once

#include <memory>

#include "media_core/interfaces/capability_probe.h"
#include "media_core/interfaces/demuxer.h"
#include "media_core/interfaces/frame_converter.h"
#include "media_core/interfaces/muxer.h"
#include "media_core/interfaces/video_decoder.h"
#include "media_core/interfaces/video_encoder.h"

namespace media_core {

class CodecFactory {
public:
    static std::unique_ptr<IDemuxer> CreateDemuxer();
    static std::unique_ptr<IVideoDecoder> CreateVideoDecoder();
    static std::unique_ptr<IFrameConverter> CreateFrameConverter();
    static std::unique_ptr<IVideoEncoder> CreateVideoEncoder();
    static std::unique_ptr<IMuxer> CreateMuxer();
    static std::unique_ptr<ICapabilityProbe> CreateCapabilityProbe();
};

}  // namespace media_core
