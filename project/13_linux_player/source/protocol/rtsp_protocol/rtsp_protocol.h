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

class RtspProtocol : public IProtocolHandler {
public:
    RtspProtocol();
    ~RtspProtocol() override;

    // Transport configuration
    void setTransportTcp(bool use_tcp);
    void setUserAgent(const std::string& user_agent);
    void setTimeout(int timeout_ms);
    void setBufferSize(int buffer_size);

    // IProtocolHandler interface
    bool canHandle(const char* url) override;
    int open(const char* url) override;
    int read(uint8_t* buf, int size) override;
    int64_t seek(int64_t pos, int whence) override;
    int close() override;
    std::vector<std::string> getSchemes() const override;

private:
    bool use_tcp_ = true;       // TCP interleaved transport by default
    std::string user_agent_;
    int timeout_ms_ = 10000;
    int buffer_size_ = 0;       // 0 = use FFmpeg default

    AVIOContext* avio_ctx_ = nullptr;
    uint8_t* avio_buffer_ = nullptr;
    int avio_buffer_size_ = 4096;
    std::string url_;
};

} // namespace player
