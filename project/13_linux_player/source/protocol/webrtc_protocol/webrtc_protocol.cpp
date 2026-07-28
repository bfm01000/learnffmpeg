#include "webrtc_protocol.h"

namespace player {

WebRTCProtocol::WebRTCProtocol() {}

WebRTCProtocol::~WebRTCProtocol() {
    close();
}

bool WebRTCProtocol::canHandle(const char* url) {
    // TODO: check if url starts with webrtc://
    (void)url;
    return true;
}

int WebRTCProtocol::open(const char* url) {
    // TODO: implement WebRTC protocol open
    // WebRTC streaming is commonly accessed via:
    //   1. WHIP/WHEP endpoints (HTTP-based signaling + WebRTC media)
    //   2. Custom signaling protocols
    //
    // Approach:
    //   1. Parse the URL for signaling endpoint and stream credentials
    //   2. Establish WebRTC connection (ICE/STUN/TURN)
    //   3. Negotiate media via SDP offer/answer
    //   4. Receive RTP/RTCP packets via the data channel
    //   5. Feed received data into FFmpeg's avio_read()
    //
    // External libraries that may be used:
    //   - libdatachannel (C++ WebRTC library)
    //   - gstreamer webrtcbin
    //   - FFmpeg with custom WebRTC patches
    (void)url;
    return -1;
}

int WebRTCProtocol::read(uint8_t* buf, int size) {
    // TODO: implement WebRTC read
    (void)buf;
    (void)size;
    return -1;
}

int64_t WebRTCProtocol::seek(int64_t pos, int whence) {
    // WebRTC is a real-time streaming protocol, seeking is not supported
    (void)pos;
    (void)whence;
    return -1;
}

int WebRTCProtocol::close() {
    // TODO: implement WebRTC close and cleanup
    // 1. Close WebRTC peer connection
    // 2. Release ICE candidates
    // 3. Clean up signaling state
    return 0;
}

std::vector<std::string> WebRTCProtocol::getSchemes() const {
    // TODO: WebRTC scheme — may be "webrtc" or could use "whip"/"whep" schemes
    return {"webrtc"};
}

} // namespace player
