#pragma once

#include <string>
#include <vector>

namespace media_core {

class ICapabilityProbe {
public:
    virtual ~ICapabilityProbe() = default;
    virtual std::vector<std::string> QueryDecoderHwDevices(int codec_id) const = 0;
};

}  // namespace media_core
