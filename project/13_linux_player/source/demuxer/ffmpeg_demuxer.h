#pragma once

#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include "i_media_source.h"
#include "stream_info.h"
#include "../protocol/i_protocol_handler.h"

namespace player {

class FFmpegDemuxer : public IMediaSource {
public:
    FFmpegDemuxer();
    explicit FFmpegDemuxer(std::unique_ptr<IProtocolHandler> protocol_handler);
    ~FFmpegDemuxer() override;

    int open(const char* url) override;
    std::shared_ptr<AVPacket> readPacket() override;
    int seekTo(int64_t pos_ms) override;
    std::vector<StreamInfo> getStreams() const override;
    int close() override;

    AVFormatContext* formatContext() const;

private:
    void buildStreamInfos();

    AVFormatContext* fmt_ctx_ = nullptr;
    std::unique_ptr<IProtocolHandler> protocol_handler_;
    std::vector<StreamInfo> stream_infos_;
    bool opened_ = false;
};

} // namespace player
