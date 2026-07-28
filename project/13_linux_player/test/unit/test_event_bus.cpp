/// @file test/unit/test_event_bus.cpp
/// @brief EventBus 单元测试.

#include "core/event/event_bus.h"
#include "core/event/event_types.h"

#include <gtest/gtest.h>
#include <string>

namespace player {
namespace test {

// ── 基础订阅/通知 ────────────────────────────────────────────────────────

TEST(EventBusTest, SubscribeAndPost) {
  EventBus bus;
  std::string received;

  bus.subscribe(EventType::Info, [&](const PlayerEvent& e) {
    received = e.message;
  });

  bus.post(PlayerEvent(EventType::Info, "hello"));
  EXPECT_EQ(received, "hello");
}

TEST(EventBusTest, MultipleSubscribers) {
  EventBus bus;
  int count = 0;

  bus.subscribe(EventType::Play, [&](const PlayerEvent&) { ++count; });
  bus.subscribe(EventType::Play, [&](const PlayerEvent&) { ++count; });

  bus.post(PlayerEvent(EventType::Play));
  EXPECT_EQ(count, 2);
}

TEST(EventBusTest, OnlyMatchingTypeReceives) {
  EventBus bus;
  int playCount  = 0;
  int pauseCount = 0;

  bus.subscribe(EventType::Play,  [&](const PlayerEvent&) { ++playCount; });
  bus.subscribe(EventType::Pause, [&](const PlayerEvent&) { ++pauseCount; });

  bus.post(PlayerEvent(EventType::Play));
  EXPECT_EQ(playCount,  1);
  EXPECT_EQ(pauseCount, 0);
}

// ── unsubscribe ──────────────────────────────────────────────────────────

TEST(EventBusTest, UnsubscribeRemovesHandler) {
  EventBus bus;
  int count = 0;

  size_t id = bus.subscribe(EventType::Error, [&](const PlayerEvent&) {
    ++count;
  });

  bus.post(PlayerEvent(EventType::Error));
  EXPECT_EQ(count, 1);

  bus.unsubscribe(EventType::Error, id);
  bus.post(PlayerEvent(EventType::Error));
  EXPECT_EQ(count, 1);   // 不应再增加
}

TEST(EventBusTest, UnsubscribeNonexistent) {
  EventBus bus;
  // 不应崩溃
  bus.unsubscribe(EventType::Info, 99999);
}

// ── emit + dispatch ──────────────────────────────────────────────────────

TEST(EventBusTest, EmitThenDispatch) {
  EventBus bus;
  int count = 0;

  bus.subscribe(EventType::StateChanged, [&](const PlayerEvent&) {
    ++count;
  });

  bus.emit(PlayerEvent(EventType::StateChanged));
  bus.emit(PlayerEvent(EventType::StateChanged));
  EXPECT_EQ(count, 0);   // emit 不触发

  bus.dispatch();
  EXPECT_GE(count, 1);   // dispatch 触发
}

// ── clear ────────────────────────────────────────────────────────────────

TEST(EventBusTest, ClearType) {
  EventBus bus;
  int count = 0;

  bus.subscribe(EventType::DroppedFrames, [&](const PlayerEvent&) {
    ++count;
  });

  bus.clear(EventType::DroppedFrames);
  bus.post(PlayerEvent(EventType::DroppedFrames));
  EXPECT_EQ(count, 0);
}

TEST(EventBusTest, ClearAll) {
  EventBus bus;
  int a = 0, b = 0;

  bus.subscribe(EventType::Play,  [&](const PlayerEvent&) { ++a; });
  bus.subscribe(EventType::Pause, [&](const PlayerEvent&) { ++b; });

  bus.clearAll();

  bus.post(PlayerEvent(EventType::Play));
  bus.post(PlayerEvent(EventType::Pause));
  EXPECT_EQ(a, 0);
  EXPECT_EQ(b, 0);
}

TEST(EventBusTest, SubscriberCount) {
  EventBus bus;
  EXPECT_EQ(bus.subscriberCount(EventType::Play), 0);

  bus.subscribe(EventType::Play, [](const PlayerEvent&) {});
  EXPECT_EQ(bus.subscriberCount(EventType::Play), 1);

  bus.subscribe(EventType::Play, [](const PlayerEvent&) {});
  EXPECT_EQ(bus.subscriberCount(EventType::Play), 2);
}

// ── 空 handler ───────────────────────────────────────────────────────────

TEST(EventBusTest, NullHandlerDoesNotCrash) {
  EventBus bus;
  bus.subscribe(EventType::Info, nullptr);
  bus.post(PlayerEvent(EventType::Info, "test"));   // 不应崩溃
}

} // namespace test
} // namespace player
