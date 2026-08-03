#pragma once

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include "../i_protocol_handler.h"

namespace player {

class RtmpProtocol : public IProtocolHandler {
public:
    RtmpProtocol();
    ~RtmpProtocol() override;

    // RTMP-specific options
    void setApp(const std::string& app);
    void setFlashVer(const std::string& flash_ver);
    void setSwfUrl(const std::string& swf_url);
    void setPageUrl(const std::string& page_url);
    void setTcurl(const std::string& tcurl);
    void setLive(bool live);

    // IProtocolHandler interface
    bool canHandle(const char* url) override;
    int open(const char* url) override;
    int read(uint8_t* buf, int size) override;
    int64_t seek(int64_t pos, int whence) override;
    int close() override;
    std::vector<std::string> getSchemes() const override;

private:
    std::string m_app;
    std::string m_flashVer;
    std::string m_swfUrl;
    std::string m_pageUrl;
    std::string m_tcurl;
    bool m_live = true;

    AVIOContext* m_avioCtx = nullptr;
    uint8_t* m_avioBuffer = nullptr;
    int m_avioBufferSize = 4096;
    std::string m_url;
};

} // namespace player
