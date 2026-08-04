#include "rtmp_protocol.h"

namespace player {

RtmpProtocol::RtmpProtocol() {}

RtmpProtocol::~RtmpProtocol() {
    close();
}

void RtmpProtocol::setApp(const std::string& app) {
    m_app = app;
}

void RtmpProtocol::setFlashVer(const std::string& flash_ver) {
    m_flashVer = flash_ver;
}

void RtmpProtocol::setSwfUrl(const std::string& swf_url) {
    m_swfUrl = swf_url;
}

void RtmpProtocol::setPageUrl(const std::string& page_url) {
    m_pageUrl = page_url;
}

void RtmpProtocol::setTcurl(const std::string& tcurl) {
    m_tcurl = tcurl;
}

void RtmpProtocol::setLive(bool live) {
    m_live = live;
}

bool RtmpProtocol::canHandle(const char* url) {
    if (!url) return false;
    std::string u(url);
    return u.rfind("rtmp", 0) == 0; // rtmp:// rtmpe:// rtmps:// rtmpt://
}

int RtmpProtocol::open(const char* url) {
    if (!url || !*url) return -1;

    close();
    m_url = url;

    // Build RTMP options dictionary
    AVDictionary* opts = nullptr;
    if (!m_app.empty())      av_dict_set(&opts, "rtmp_app",       m_app.c_str(), 0);
    if (!m_flashVer.empty()) av_dict_set(&opts, "rtmp_flashver",  m_flashVer.c_str(), 0);
    if (!m_swfUrl.empty())   av_dict_set(&opts, "rtmp_swfurl",    m_swfUrl.c_str(), 0);
    if (!m_pageUrl.empty())  av_dict_set(&opts, "rtmp_pageurl",   m_pageUrl.c_str(), 0);
    if (!m_tcurl.empty())    av_dict_set(&opts, "rtmp_tcurl",     m_tcurl.c_str(), 0);
    av_dict_set_int(&opts, "rtmp_live", m_live ? 1 : 0, 0);

    // FFmpeg has built-in RTMP protocol — avio_open2 handles it
    int ret = avio_open2(&m_avioCtx, url, AVIO_FLAG_READ, nullptr, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        m_avioCtx = nullptr;
        return ret;
    }
    return 0;
}

int RtmpProtocol::read(uint8_t* buf, int size) {
    // TODO: read RTMP data via avio_read()
    if (!m_avioCtx) return -1;
    int ret = avio_read(m_avioCtx, buf, size);
    if (ret == AVERROR_EOF) {
        return 0;
    }
    return ret;
}

int64_t RtmpProtocol::seek(int64_t pos, int whence) {
    // RTMP protocol does not support seeking
    (void)pos;
    (void)whence;
    return -1;
}

int RtmpProtocol::close() {
    if (m_avioCtx) {
        avio_close(m_avioCtx); // also frees internal buffer
        m_avioCtx = nullptr;
    }
    m_url.clear();
    return 0;
}

std::vector<std::string> RtmpProtocol::getSchemes() const {
    return {"rtmp"};
}

} // namespace player
