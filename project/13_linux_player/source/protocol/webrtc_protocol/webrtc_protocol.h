#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../i_protocol_handler.h"

namespace player {

class WebRTCProtocol : public IProtocolHandler {
public:
    WebRTCProtocol();
    ~WebRTCProtocol() override;

    // IProtocolHandler interface
    bool canHandle(const char* url) override;
    int open(const char* url) override;
    int read(uint8_t* buf, int size) override;
    int64_t seek(int64_t pos, int whence) override;
    int close() override;
    std::vector<std::string> getSchemes() const override;

private:
    // TODO: WebRTC protocol implementation
    // WebRTC for streaming typically uses WHIP (WebRTC-HTTP ingestion protocol) or WHEP (WebRTC-HTTP egress protocol).
    // Implementation options:
    //   1. FFmpeg with --enable-libwebrtc (via system webrtc or custom patches)
    //   2. GStreamer's webrtcbin as a backend
    //   3. Direct integration with libdatachannel or similar
    void* handle_ = nullptr;
    std::string url_;
};

} // namespace player
