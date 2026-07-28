#include "rtmp_protocol.h"

namespace player {

RtmpProtocol::RtmpProtocol() {}

RtmpProtocol::~RtmpProtocol() {
    close();
}

void RtmpProtocol::setApp(const std::string& app) {
    app_ = app;
}

void RtmpProtocol::setFlashVer(const std::string& flash_ver) {
    flash_ver_ = flash_ver;
}

void RtmpProtocol::setSwfUrl(const std::string& swf_url) {
    swf_url_ = swf_url;
}

void RtmpProtocol::setPageUrl(const std::string& page_url) {
    page_url_ = page_url;
}

void RtmpProtocol::setTcurl(const std::string& tcurl) {
    tcurl_ = tcurl;
}

void RtmpProtocol::setLive(bool live) {
    live_ = live;
}

bool RtmpProtocol::canHandle(const char* url) {
    // TODO: check if url starts with rtmp://, rtmpe://, rtmps://, rtmpt://, etc.
    return true;
}

int RtmpProtocol::open(const char* url) {
    // TODO: open RTMP stream via FFmpeg's built-in RTMP protocol (libavformat)
    // 1. Build AVDictionary with RTMP options (rtmp_app, rtmp_flashver, rtmp_swfurl, etc.)
    // 2. Allocate AVIOContext via avio_open2()
    // 3. The ffmpeg rtmp protocol is built-in and handles the RTMP handshake
    // 4. Return 0 on success, negative on error
    url_ = url;
    return -1;
}

int RtmpProtocol::read(uint8_t* buf, int size) {
    // TODO: read RTMP data via avio_read()
    if (!avio_ctx_) return -1;
    int ret = avio_read(avio_ctx_, buf, size);
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
    // TODO: close RTMP stream and cleanup
    if (avio_ctx_) {
        avio_close(avio_ctx_);
        avio_ctx_ = nullptr;
    }
    if (avio_buffer_) {
        av_freep(&avio_buffer_);
    }
    url_.clear();
    return 0;
}

std::vector<std::string> RtmpProtocol::getSchemes() const {
    return {"rtmp"};
}

} // namespace player
