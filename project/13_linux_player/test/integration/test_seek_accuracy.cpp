/// @brief Seek accuracy integration tests

#include "player_controller.h"
#include <gtest/gtest.h>

namespace player {
namespace test {

TEST(SeekAccuracyTest, SeekToKeyframe) {
  // TODO: test seek to I-frame position
  EXPECT_TRUE(true);
}

TEST(SeekAccuracyTest, SeekToNonKeyframe) {
  // TODO: test accurate seek decodes to exact PTS
}

TEST(SeekAccuracyTest, SeekBackward) {
  // TODO: test backward seek maintains AV sync
}

TEST(SeekAccuracyTest, RapidSeek) {
  // TODO: test rapid consecutive seeks don't crash or hang
}

TEST(SeekAccuracyTest, SeekToEnd) {
  // TODO: test seek near EOS triggers completion
}

} // namespace test
} // namespace player
