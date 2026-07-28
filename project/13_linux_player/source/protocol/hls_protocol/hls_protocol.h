#pragma once

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include "../i_protocol_handler.h"

namespace player {

struct VariantStream {
    int bandwidth = 0;
    std::string resolution;
    std::string codecs;
    std::string url;
};

class HlsProtocol : public IProtocolHandler {
public:
    HlsProtocol();
    ~HlsProtocol() override;

    // Configuration
    void setMaxReloadCount(int count);
    void setLiveStartIndex(int index);

    // Multi-bitrate variant query
    std::vector<VariantStream> getVariants() const;
    bool selectVariant(int bandwidth);

    // IProtocolHandler interface
    bool canHandle(const char* url) override;
    int open(const char* url) override;
    int read(uint8_t* buf, int size) override;
    int64_t seek(int64_t pos, int whence) override;
    int close() override;
    std::vector<std::string> getSchemes() const override;

private:
    int max_reload_count_ = 1000;
    int live_start_index_ = -1; // -1 = default (most recent segment)

    AVIOContext* avio_ctx_ = nullptr;
    uint8_t* avio_buffer_ = nullptr;
    int avio_buffer_size_ = 65536; // larger buffer for HLS segments
    std::string url_;
    std::vector<VariantStream> variants_;
};

} // namespace player
