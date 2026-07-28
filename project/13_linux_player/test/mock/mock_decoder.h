#pragma once

#include "decode/i_decoder.h"
#include <gmock/gmock.h>

namespace player {
namespace test {

class MockDecoder : public IDecoder {
public:
  MOCK_METHOD(int, open, (AVCodecParameters* params), (override));
  MOCK_METHOD(int, sendPacket, (AVPacket* pkt), (override));
  MOCK_METHOD(int, recvFrame, (AVFrame* frame), (override));
  MOCK_METHOD(void, flush, (), (override));
  MOCK_METHOD(void, close, (), (override));
};

} // namespace test
} // namespace player
