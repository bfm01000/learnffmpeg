#include "mock_demuxer.h"

namespace player {
namespace test {

void MockDemuxer::setStreams(std::vector<StreamInfo> streams) {
  // TODO: store for getStreams() return
  (void)streams;
}

} // namespace test
} // namespace player
