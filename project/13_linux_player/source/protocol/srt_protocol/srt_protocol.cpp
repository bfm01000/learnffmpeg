#include "srt_protocol.h"

namespace player {

SrtProtocol::SrtProtocol() {}

SrtProtocol::~SrtProtocol() {
    close();
}

bool SrtProtocol::canHandle(const char* url) {
    // TODO: check if url starts with srt://
    (void)url;
    return true;
}

int SrtProtocol::open(const char* url) {
    // TODO: implement SRT protocol open
    // SRT support is available via FFmpeg's built-in SRT protocol (requires --enable-libsrt)
    // 1. Use avio_open2() with "srt://" URL scheme
    // 2. Configure SRT options:
    //    - latency: push buffer latency in ms
    //    - maxbw: maximum bandwidth (0 = auto)
    //    - passphrase: encryption passphrase
    //    - pbkeylen: crypto key length (16, 24, or 32)
    //    - srt_streamid: stream ID for connection
    (void)url;
    return -1;
}

int SrtProtocol::read(uint8_t* buf, int size) {
    // TODO: implement SRT read
    (void)buf;
    (void)size;
    return -1;
}

int64_t SrtProtocol::seek(int64_t pos, int whence) {
    // SRT is a streaming protocol, seeking is not supported
    (void)pos;
    (void)whence;
    return -1;
}

int SrtProtocol::close() {
    // TODO: implement SRT close
    return 0;
}

std::vector<std::string> SrtProtocol::getSchemes() const {
    return {"srt"};
}

} // namespace player
