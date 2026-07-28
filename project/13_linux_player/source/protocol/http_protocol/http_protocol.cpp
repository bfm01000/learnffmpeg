#include "http_protocol.h"

#include <sstream>

namespace player {

HttpProtocol::HttpProtocol() {}

HttpProtocol::~HttpProtocol() {
    close();
}

void HttpProtocol::setHeaders(const std::map<std::string, std::string>& headers) {
    headers_ = headers;
}

void HttpProtocol::setTimeout(int timeout_ms) {
    timeout_ms_ = timeout_ms;
}

void HttpProtocol::setReconnect(bool enabled, int max_retries) {
    reconnect_ = enabled;
    max_retries_ = max_retries;
}

bool HttpProtocol::canHandle(const char* url) {
    // TODO: check if url starts with http:// or https://
    return true;
}

int HttpProtocol::open(const char* url) {
    // TODO: open HTTP stream via FFmpeg's AVIO
    // 1. Build AVDictionary with HTTP options (headers, timeout, reconnect, user-agent)
    // 2. Allocate avio_buffer_ via av_malloc()
    // 3. Open AVIOContext via avio_open2() with custom options
    // 4. Return 0 on success, negative on error
    url_ = url;
    return -1;
}

int HttpProtocol::read(uint8_t* buf, int size) {
    // TODO: read from HTTP stream via avio_read()
    if (!avio_ctx_) return -1;
    int ret = avio_read(avio_ctx_, buf, size);
    if (ret == AVERROR_EOF) {
        return 0;
    }
    return ret;
}

int64_t HttpProtocol::seek(int64_t pos, int whence) {
    // TODO: seek in HTTP stream via avio_seek()
    // HTTP seeking requires the server to support Range requests
    if (!avio_ctx_) return -1;
    return avio_seek(avio_ctx_, pos, whence);
}

int HttpProtocol::close() {
    // TODO: close HTTP stream and cleanup
    // 1. avio_close(avio_ctx_)
    // 2. av_freep(&avio_buffer_)
    // 3. avformat_close_input(&fmt_ctx_) if needed
    if (avio_ctx_) {
        avio_close(avio_ctx_);
        avio_ctx_ = nullptr;
    }
    if (avio_buffer_) {
        av_freep(&avio_buffer_);
    }
    if (fmt_ctx_) {
        avformat_close_input(&fmt_ctx_);
    }
    url_.clear();
    return 0;
}

std::vector<std::string> HttpProtocol::getSchemes() const {
    return {"http", "https"};
}

std::string HttpProtocol::buildAVIOOptions() const {
    // TODO: build FFmpeg AVIO options string from configuration
    // e.g., "headers=..." "timeout=..." "reconnect=1" "reconnect_at_eof=1"
    std::ostringstream oss;
    if (!headers_.empty()) {
        oss << "headers=";
        for (const auto& [key, val] : headers_) {
            oss << key << ": " << val << "\r\n";
        }
    }
    oss << "timeout=" << timeout_ms_;
    if (reconnect_) {
        oss << ":reconnect=1:reconnect_at_eof=1";
        oss << ":reconnect_max_retries=" << max_retries_;
    }
    return oss.str();
}

} // namespace player
