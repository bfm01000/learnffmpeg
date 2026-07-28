#include "av_sync_engine.h"
#include "clock_manager.h"
#include <gtest/gtest.h>

namespace player {
namespace test {

TEST(AVSyncTest, CalcDelayAudioMaster) {
  // TODO: test delay calculation with audio master clock
  EXPECT_TRUE(true);
}

TEST(AVSyncTest, DropThreshold) {
  // TODO: test frame drop when delay < drop_threshold
}

TEST(AVSyncTest, SleepThreshold) {
  // TODO: test frame sleep-wait when delay > tolerance
}

} // namespace test
} // namespace player
