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

    std::map<std::string, std::string> m_headers;
    int m_timeoutMs = 10000;
    bool m_reconnect = true;
    int m_maxRetries = 3;

    // FFmpeg I/O state
    AVIOContext* m_avioCtx = nullptr;
    AVFormatContext* m_fmtCtx = nullptr;
    uint8_t* m_avioBuffer = nullptr;
    int m_avioBufferSize = 4096;
    std::string m_url;
};

} // namespace player
