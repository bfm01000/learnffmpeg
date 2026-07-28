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
    std::string app_;
    std::string flash_ver_;
    std::string swf_url_;
    std::string page_url_;
    std::string tcurl_;
    bool live_ = true;

    AVIOContext* avio_ctx_ = nullptr;
    uint8_t* avio_buffer_ = nullptr;
    int avio_buffer_size_ = 4096;
    std::string url_;
};

} // namespace player
