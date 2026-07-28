#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace player {

class IProtocolHandler {
public:
    virtual ~IProtocolHandler() = default;

    virtual bool canHandle(const char* url) = 0;
    virtual int open(const char* url) = 0;
    virtual int read(uint8_t* buf, int size) = 0;
    virtual int64_t seek(int64_t pos, int whence) = 0;
    virtual int close() = 0;
    virtual std::vector<std::string> getSchemes() const = 0;
};

} // namespace player
