/// @brief Local MP4 playback integration tests

#include "player_controller.h"
#include <gtest/gtest.h>

namespace player {
namespace test {

TEST(LocalMp4PlaybackTest, BasicPlayback) {
  // TODO: create test MP4, play for 5 seconds, verify frames decoded
  EXPECT_TRUE(true);
}

TEST(LocalMp4PlaybackTest, LoopPlayback) {
  // TODO: test loop mode plays back-to-back
}

TEST(LocalMp4PlaybackTest, MultiStreamAudioVideo) {
  // TODO: test MP4 with multiple audio tracks
}

} // namespace test
} // namespace player
