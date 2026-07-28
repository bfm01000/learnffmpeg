#include "hls_protocol.h"

namespace player {

HlsProtocol::HlsProtocol() {}

HlsProtocol::~HlsProtocol() {
    close();
}

void HlsProtocol::setMaxReloadCount(int count) {
    max_reload_count_ = count;
}

void HlsProtocol::setLiveStartIndex(int index) {
    live_start_index_ = index;
}

std::vector<VariantStream> HlsProtocol::getVariants() const {
    return variants_;
}

bool HlsProtocol::selectVariant(int bandwidth) {
    // TODO: switch to variant stream matching the given bandwidth
    // 1. Find variant with bandwidth closest to but not exceeding the target
    // 2. Update url_ to the selected variant playlist
    // 3. Re-open connection if currently streaming
    (void)bandwidth;
    return false;
}

bool HlsProtocol::canHandle(const char* url) {
    // TODO: check if url ends with .m3u8 or starts with hls://
    return true;
}

int HlsProtocol::open(const char* url) {
    // TODO: open HLS stream via FFmpeg's built-in HLS demuxer
    // 1. Build AVDictionary with HLS options:
    //    - hls_reload_count: max segment reload attempts for live
    //    - live_start_index: which segment to start from
    //    - http_persistent: keep HTTP connection alive
    // 2. FFmpeg's HLS demuxer handles:
    //    - Loading the master playlist (variant streams)
    //    - Loading media playlists
    //    - Downloading and sequencing TS/fMP4 segments
    // 3. If master playlist is detected, populate variants_
    // 4. Return 0 on success, negative on error
    url_ = url;
    return -1;
}

int HlsProtocol::read(uint8_t* buf, int size) {
    // TODO: read HLS segment data via avio_read()
    if (!avio_ctx_) return -1;
    int ret = avio_read(avio_ctx_, buf, size);
    if (ret == AVERROR_EOF) {
        return 0;
    }
    return ret;
}

int64_t HlsProtocol::seek(int64_t pos, int whence) {
    // TODO: seek in HLS stream
    // HLS supports seeking in VOD content; for live streams seek is limited
    if (!avio_ctx_) return -1;
    return avio_seek(avio_ctx_, pos, whence);
}

int HlsProtocol::close() {
    // TODO: close HLS stream and cleanup
    if (avio_ctx_) {
        avio_close(avio_ctx_);
        avio_ctx_ = nullptr;
    }
    if (avio_buffer_) {
        av_freep(&avio_buffer_);
    }
    variants_.clear();
    url_.clear();
    return 0;
}

std::vector<std::string> HlsProtocol::getSchemes() const {
    return {"hls", "m3u8"};
}

} // namespace player
