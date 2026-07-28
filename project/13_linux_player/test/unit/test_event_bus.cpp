#include "event_bus.h"
#include "event_types.h"
#include <gtest/gtest.h>

namespace player {
namespace test {

TEST(EventBusTest, SubscribeEmit) {
  // TODO: test event subscription and emission
  EXPECT_TRUE(true);
}

TEST(EventBusTest, Unsubscribe) {
  // TODO: test unsubscribe stops receiving events
}

TEST(EventBusTest, AsyncDispatch) {
  // TODO: test async event dispatch thread safety
}

} // namespace test
} // namespace player
