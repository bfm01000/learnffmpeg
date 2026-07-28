#include "ffmpeg_demuxer.h"
#include "stream_info.h"
#include <gtest/gtest.h>

namespace player {
namespace test {

TEST(DemuxerTest, OpenLocalFile) {
  // TODO: test opening a local MP4 file
  EXPECT_TRUE(true);
}

TEST(DemuxerTest, StreamInfo) {
  // TODO: test getStreams returns correct info
}

TEST(DemuxerTest, ReadPackets) {
  // TODO: test readPacket returns valid AVPackets
}

TEST(DemuxerTest, Seek) {
  // TODO: test seekTo and subsequent readPacket
}

} // namespace test
} // namespace player
