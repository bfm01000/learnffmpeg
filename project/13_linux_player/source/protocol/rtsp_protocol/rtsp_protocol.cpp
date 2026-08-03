#include "rtsp_protocol.h"

namespace player {

RtspProtocol::RtspProtocol() {}

RtspProtocol::~RtspProtocol() {
    close();
}

void RtspProtocol::setTransportTcp(bool use_tcp) {
    m_useTcp = use_tcp;
}

void RtspProtocol::setUserAgent(const std::string& user_agent) {
    m_userAgent = user_agent;
}

void RtspProtocol::setTimeout(int timeout_ms) {
    m_timeoutMs = timeout_ms;
}

void RtspProtocol::setBufferSize(int buffer_size) {
    m_bufferSize = buffer_size;
}

bool RtspProtocol::canHandle(const char* url) {
    // TODO: check if url starts with rtsp:// or rtsps://
    return true;
}

int RtspProtocol::open(const char* url) {
    // TODO: open RTSP stream via FFmpeg's built-in RTSP protocol
    // 1. Build AVDictionary with RTSP options:
    //    - rtsp_transport: "tcp" or "udp"
    //    - user_agent: custom UA string
    //    - timeout: socket timeout
    //    - buffer_size: socket buffer size
    // 2. Allocate AVIOContext via avio_open2() with the dictionary
    // 3. FFmpeg's built-in RTSP demuxer handles SDP parsing and RTP/RTCP
    // 4. Return 0 on success, negative on error
    m_url = url;
    return -1;
}

int RtspProtocol::read(uint8_t* buf, int size) {
    // TODO: read RTSP/RTP data via avio_read()
    if (!m_avioCtx) return -1;
    int ret = avio_read(m_avioCtx, buf, size);
    if (ret == AVERROR_EOF) {
        return 0;
    }
    return ret;
}

int64_t RtspProtocol::seek(int64_t pos, int whence) {
    // TODO: seek in RTSP stream
    // RTSP supports seeking via PLAY with Range header for VOD content
    // Live streams do not support seeking
    if (!m_avioCtx) return -1;
    return avio_seek(m_avioCtx, pos, whence);
}

int RtspProtocol::close() {
    // TODO: close RTSP stream and cleanup
    // TEARDOWN is sent via avio_close()
    if (m_avioCtx) {
        avio_close(m_avioCtx);
        m_avioCtx = nullptr;
    }
    if (m_avioBuffer) {
        av_freep(&m_avioBuffer);
    }
    m_url.clear();
    return 0;
}

std::vector<std::string> RtspProtocol::getSchemes() const {
    return {"rtsp"};
}

} // namespace player
