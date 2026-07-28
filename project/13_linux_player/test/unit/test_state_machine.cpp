#include "state_machine.h"
#include "event_types.h"
#include <gtest/gtest.h>

namespace player {
namespace test {

TEST(StateMachineTest, InitialState) {
  // TODO: test initial state is Idle
  EXPECT_TRUE(true);
}

TEST(StateMachineTest, ValidTransition) {
  // TODO: test Idle -> Loading transition on OPEN event
}

TEST(StateMachineTest, InvalidTransition) {
  // TODO: test invalid transition returns false
}

TEST(StateMachineTest, FullPlaybackCycle) {
  // TODO: test Idle -> Loading -> Ready -> Playing -> Completed -> Idle
}

} // namespace test
} // namespace player
