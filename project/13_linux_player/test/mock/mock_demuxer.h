#pragma once

#include "source/demuxer/i_media_source.h"
#include <gmock/gmock.h>
#include <vector>

namespace player {
namespace test {

class MockDemuxer : public IMediaSource {
public:
  MOCK_METHOD(int, open, (const char* url), (override));
  MOCK_METHOD(std::shared_ptr<AVPacket>, readPacket, (), (override));
  MOCK_METHOD(int, seekTo, (int64_t position_ms), (override));
  MOCK_METHOD(std::vector<StreamInfo>, getStreams, (), (const, override));
  MOCK_METHOD(int, close, (), (override));

  // Helper: populate canned stream info
  void setStreams(std::vector<StreamInfo> streams);
};

} // namespace test
} // namespace player
