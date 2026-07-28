/// @file test/unit/test_packet_queue.cpp
/// @brief PacketQueue 单元测试 —— int 类型由 packet_queue.cpp 显式实例化提供.

#include "core/queue/packet_queue.h"
#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace player {
namespace test {

// ── 基础 push/pop ─────────────────────────────────────────────────────────

TEST(PacketQueueTest, PushPopSingle) {
  PacketQueue<int> q(16);
  ASSERT_TRUE(q.push(42));
  auto item = q.pop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(*item, 42);
}

TEST(PacketQueueTest, PushPopMultiple) {
  PacketQueue<int> q(16);
  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(q.push(i));
  }
  EXPECT_EQ(q.size(), 10);
  for (int i = 0; i < 10; ++i) {
    auto item = q.pop();
    ASSERT_TRUE(item.has_value());
    EXPECT_EQ(*item, i);
  }
}

TEST(PacketQueueTest, MoveSemantics) {
  // 验证支持 move-only 语义: 用 unique_ptr 测试
  PacketQueue<std::unique_ptr<int>> q(4);
  ASSERT_TRUE(q.push(std::make_unique<int>(99)));
  auto item = q.pop();
  ASSERT_TRUE(item.has_value());
  ASSERT_NE(*item, nullptr);
  EXPECT_EQ(**item, 99);
}

// ── 容量限制 ──────────────────────────────────────────────────────────────

TEST(PacketQueueTest, CapacityEnforced) {
  PacketQueue<int> q(3);
  ASSERT_TRUE(q.push(1));
  ASSERT_TRUE(q.push(2));
  ASSERT_TRUE(q.push(3));
  EXPECT_EQ(q.size(), 3);
  // 非阻塞 push 应 fail
  EXPECT_FALSE(q.push(4, 0));
  EXPECT_EQ(q.size(), 3);
}

TEST(PacketQueueTest, BlockOnFullThenPop) {
  PacketQueue<int> q(2);
  q.push(1);
  q.push(2);

  // 另一个线程等空位
  bool pushed = false;
  std::thread t([&] {
    pushed = q.push(3, 500); // 500ms 超时
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_FALSE(pushed); // 还在等

  q.pop(); // 空出一个位置
  t.join();
  EXPECT_TRUE(pushed);
}

// ── 阻塞 pop ──────────────────────────────────────────────────────────────

TEST(PacketQueueTest, PopBlocksUntilData) {
  PacketQueue<int> q(4);
  std::optional<int> result;

  std::thread consumer([&] {
    result = q.pop(500);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_FALSE(result.has_value()); // 还在等

  q.push(7);
  consumer.join();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 7);
}

TEST(PacketQueueTest, PopTimeout) {
  PacketQueue<int> q(4);
  auto item = q.pop(100);
  EXPECT_FALSE(item.has_value()); // 超时
}

// ── Flush ─────────────────────────────────────────────────────────────────

TEST(PacketQueueTest, FlushClearsQueue) {
  PacketQueue<int> q(8);
  q.push(1); q.push(2); q.push(3);

  q.flush();
  EXPECT_EQ(q.size(), 1); // flush token 在里面
}

TEST(PacketQueueTest, FlushTokenReceived) {
  PacketQueue<int> q(8);
  q.push(10);
  q.push(20);
  q.flush();

  // 旧的 packet 已被清除
  auto item = q.pop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(*item, 0); // T{} = 0 int, 即 flush token
}

TEST(PacketQueueTest, SerialIncrementsOnFlush) {
  PacketQueue<int> q(8);

  EXPECT_EQ(q.serial(), 0);
  q.flush();
  EXPECT_EQ(q.serial(), 1);
  q.flush();
  EXPECT_EQ(q.serial(), 2);
}

TEST(PacketQueueTest, PushAfterFlush) {
  PacketQueue<int> q(8);
  q.flush();

  // Consumer 读到 flush token
  auto token = q.pop();
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(*token, 0);

  // Producer 继续 push 新数据
  q.push(77);
  auto item = q.pop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(*item, 77);
}

// ── Abort ─────────────────────────────────────────────────────────────────

TEST(PacketQueueTest, AbortWakesPop) {
  PacketQueue<int> q(4);
  std::optional<int> result;
  bool completed = false;

  std::thread consumer([&] {
    result = q.pop(); // 无限阻塞
    completed = true;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_FALSE(completed);

  q.abort();
  consumer.join();
  EXPECT_TRUE(completed);
  EXPECT_FALSE(result.has_value()); // abort 返回 nullopt
}

TEST(PacketQueueTest, AbortWakesPush) {
  PacketQueue<int> q(1);
  q.push(1); // 满

  bool pushResult = true;
  std::thread producer([&] {
    pushResult = q.push(2); // 阻塞等空位
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  q.abort();
  producer.join();
  EXPECT_FALSE(pushResult); // abort 后 push 返回 false
}

TEST(PacketQueueTest, PushAfterAbort) {
  PacketQueue<int> q(4);
  q.abort();
  EXPECT_FALSE(q.push(1, 100));
}

TEST(PacketQueueTest, PopAfterAbortWithData) {
  // abort 时如果队列还有数据, pop 应该返回数据而不是 nullopt
  PacketQueue<int> q(4);
  q.push(5);
  q.abort();

  auto item = q.pop(); // 队列非空, 应返回数据
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(*item, 5);

  // 之后队列空, 应返回 nullopt
  auto empty = q.pop();
  EXPECT_FALSE(empty.has_value());
}

// ── 查询 ─────────────────────────────────────────────────────────────────

TEST(PacketQueueTest, SizeReflectsElements) {
  PacketQueue<int> q(10);
  EXPECT_EQ(q.size(), 0);
  q.push(1); q.push(2);
  EXPECT_EQ(q.size(), 2);
  q.pop();
  EXPECT_EQ(q.size(), 1);
}

TEST(PacketQueueTest, CapacityReturnsMax) {
  PacketQueue<int> q(42);
  EXPECT_EQ(q.capacity(), 42);
}

TEST(PacketQueueTest, IsAborted) {
  PacketQueue<int> q(4);
  EXPECT_FALSE(q.isAborted());
  q.abort();
  EXPECT_TRUE(q.isAborted());
}

// ── Reset ─────────────────────────────────────────────────────────────────

TEST(PacketQueueTest, ResetClearsAbort) {
  PacketQueue<int> q(4);
  q.push(1);
  q.abort();
  EXPECT_TRUE(q.isAborted());
  EXPECT_EQ(q.size(), 1);

  q.reset();
  EXPECT_FALSE(q.isAborted());
  EXPECT_EQ(q.size(), 0);
  EXPECT_EQ(q.serial(), 0);

  // reset 后可正常使用
  EXPECT_TRUE(q.push(99));
  auto item = q.pop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(*item, 99);
}

} // namespace test
} // namespace player
