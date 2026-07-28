#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../i_protocol_handler.h"

namespace player {

class SrtProtocol : public IProtocolHandler {
public:
    SrtProtocol();
    ~SrtProtocol() override;

    // IProtocolHandler interface
    bool canHandle(const char* url) override;
    int open(const char* url) override;
    int read(uint8_t* buf, int size) override;
    int64_t seek(int64_t pos, int whence) override;
    int close() override;
    std::vector<std::string> getSchemes() const override;

private:
    // TODO: SRT protocol implementation
    // SRT (Secure Reliable Transport) is a transport protocol for low-latency video streaming.
    // Implementation will require libsrt or FFmpeg's built-in SRT protocol (avformat).
    void* handle_ = nullptr;
    std::string url_;
};

} // namespace player
