#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include "../i_protocol_handler.h"

namespace player {

class HttpProtocol : public IProtocolHandler {
public:
    HttpProtocol();
    ~HttpProtocol() override;

    // Configuration
    void setHeaders(const std::map<std::string, std::string>& headers);
    void setTimeout(int timeout_ms);
    void setReconnect(bool enabled, int max_retries = 3);

    // IProtocolHandler interface
    bool canHandle(const char* url) override;
    int open(const char* url) override;
    int read(uint8_t* buf, int size) override;
    int64_t seek(int64_t pos, int whence) override;
    int close() override;
    std::vector<std::string> getSchemes() const override;

private:
    std::string buildAVIOOptions() const;

    std::map<std::string, std::string> headers_;
    int timeout_ms_ = 10000;
    bool reconnect_ = true;
    int max_retries_ = 3;

    // FFmpeg I/O state
    AVIOContext* avio_ctx_ = nullptr;
    AVFormatContext* fmt_ctx_ = nullptr;
    uint8_t* avio_buffer_ = nullptr;
    int avio_buffer_size_ = 4096;
    std::string url_;
};

} // namespace player
