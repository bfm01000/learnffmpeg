#include "http_protocol.h"

#include <sstream>

namespace player {

HttpProtocol::HttpProtocol() {}

HttpProtocol::~HttpProtocol() {
    close();
}

void HttpProtocol::setHeaders(const std::map<std::string, std::string>& headers) {
    m_headers = headers;
}

void HttpProtocol::setTimeout(int timeout_ms) {
    m_timeoutMs = timeout_ms;
}

void HttpProtocol::setReconnect(bool enabled, int max_retries) {
    m_reconnect = enabled;
    m_maxRetries = max_retries;
}

bool HttpProtocol::canHandle(const char* url) {
    if (!url) return false;
    std::string u(url);
    return u.rfind("http://", 0) == 0 || u.rfind("https://", 0) == 0;
}

int HttpProtocol::open(const char* url) {
    if (!url || !*url) return -1;

    // Close any previous connection
    close();
    m_url = url;

    // Build HTTP options dictionary
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "user_agent", "LinuxPlayerSDK/0.1", 0);
    if (m_timeoutMs > 0) {
        av_dict_set_int(&opts, "timeout", m_timeoutMs / 1000000, 0); // microseconds to seconds
    }
    if (m_reconnect) {
        av_dict_set_int(&opts, "reconnect", 1, 0);
        av_dict_set_int(&opts, "reconnect_at_eof", 1, 0);
        av_dict_set_int(&opts, "reconnect_max_retries", m_maxRetries, 0);
    }
    for (const auto& [key, val] : m_headers) {
        av_dict_set(&opts, key.c_str(), val.c_str(), 0);
    }

    // Open AVIO — FFmpeg handles buffer allocation internally
    int ret = avio_open2(&m_avioCtx, url, AVIO_FLAG_READ, nullptr, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        m_avioCtx = nullptr;
        return ret;
    }

    return 0;
}

int HttpProtocol::read(uint8_t* buf, int size) {
    // TODO: read from HTTP stream via avio_read()
    if (!m_avioCtx) return -1;
    int ret = avio_read(m_avioCtx, buf, size);
    if (ret == AVERROR_EOF) {
        return 0;
    }
    return ret;
}

int64_t HttpProtocol::seek(int64_t pos, int whence) {
    // TODO: seek in HTTP stream via avio_seek()
    // HTTP seeking requires the server to support Range requests
    if (!m_avioCtx) return -1;
    return avio_seek(m_avioCtx, pos, whence);
}

int HttpProtocol::close() {
    if (m_avioCtx) {
        avio_close(m_avioCtx); // also frees internal buffer
        m_avioCtx = nullptr;
    }
    if (m_fmtCtx) {
        avformat_close_input(&m_fmtCtx);
    }
    m_url.clear();
    return 0;
}

std::vector<std::string> HttpProtocol::getSchemes() const {
    return {"http", "https"};
}

std::string HttpProtocol::buildAVIOOptions() const {
    // TODO: build FFmpeg AVIO options string from configuration
    // e.g., "headers=..." "timeout=..." "reconnect=1" "reconnect_at_eof=1"
    std::ostringstream oss;
    if (!m_headers.empty()) {
        oss << "headers=";
        for (const auto& [key, val] : m_headers) {
            oss << key << ": " << val << "\r\n";
        }
    }
    oss << "timeout=" << m_timeoutMs;
    if (m_reconnect) {
        oss << ":reconnect=1:reconnect_at_eof=1";
        oss << ":reconnect_max_retries=" << m_maxRetries;
    }
    return oss.str();
}

} // namespace player
