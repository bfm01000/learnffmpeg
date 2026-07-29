/// @file test/unit/test_audio_ring_buffer.cpp
/// @brief AudioRingBuffer 单元测试.

#include "render/audio/sdl2_renderer/audio_ring_buffer.h"

#include <gtest/gtest.h>
#include <cstring>
#include <thread>

namespace player {
namespace test {

TEST(AudioRingBufferTest, WriteRead) {
  AudioRingBuffer rb(1024);
  const char* data = "hello";
  EXPECT_EQ(rb.write(reinterpret_cast<const uint8_t*>(data), 5), 5);
  EXPECT_EQ(rb.available(), 5);

  uint8_t buf[16];
  EXPECT_EQ(rb.read(buf, 5), 5);
  EXPECT_EQ(memcmp(buf, data, 5), 0);
  EXPECT_EQ(rb.available(), 0);
}

TEST(AudioRingBufferTest, WriteMoreThanCapacity) {
  AudioRingBuffer rb(8);
  const uint8_t data[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
  EXPECT_EQ(rb.write(data, 16), 8);   // 只能写 8 字节
  EXPECT_EQ(rb.available(), 8);
}

TEST(AudioRingBufferTest, ReadMoreThanAvailable) {
  AudioRingBuffer rb(64);
  uint8_t in[4] = {0xAA, 0xBB, 0xCC, 0xDD};
  rb.write(in, 4);

  uint8_t out[16];
  EXPECT_EQ(rb.read(out, 16), 4);   // 只读了 4 字节
  EXPECT_EQ(out[0], 0xAA);
  EXPECT_EQ(rb.available(), 0);
}

TEST(AudioRingBufferTest, WrapAround) {
  AudioRingBuffer rb(8);   // 2^3 = 8
  uint8_t data[8] = {1,2,3,4,5,6,7,8};

  // 写满
  EXPECT_EQ(rb.write(data, 8), 8);
  // 读一半
  uint8_t buf[4];
  EXPECT_EQ(rb.read(buf, 4), 4);
  // 再写（还剩 4 字节可写, 触发 wrap）
  EXPECT_EQ(rb.write(data, 6), 4);
  // 总共可读: 4 (剩余) + 4 (新写入) = 8
  EXPECT_EQ(rb.available(), 8);
}

TEST(AudioRingBufferTest, Clear) {
  AudioRingBuffer rb(64);
  uint8_t data[8] = {1,2,3,4,5,6,7,8};
  rb.write(data, 8);
  EXPECT_EQ(rb.available(), 8);

  rb.clear();
  EXPECT_EQ(rb.available(), 0);
  EXPECT_EQ(rb.writable(), 64);
}

TEST(AudioRingBufferTest, Writable) {
  AudioRingBuffer rb(32);
  EXPECT_EQ(rb.writable(), 32);
  rb.write(reinterpret_cast<const uint8_t*>("12345"), 5);
  EXPECT_EQ(rb.writable(), 27);
}

TEST(AudioRingBufferTest, ThreadSafetySPSC) {
  AudioRingBuffer rb(65536);
  std::atomic<bool> done{false};
  std::atomic<size_t> totalRead{0};
  std::atomic<size_t> totalWritten{0};

  // Producer thread
  std::thread producer([&] {
    uint8_t data[256];
    memset(data, 0xAB, sizeof(data));
    for (int i = 0; i < 100; ++i) {
      size_t n = rb.write(data, sizeof(data));
      totalWritten.fetch_add(n);
    }
    done.store(true);
  });

  // Consumer thread
  std::thread consumer([&] {
    uint8_t buf[256];
    while (!done.load() || rb.available() > 0) {
      size_t n = rb.read(buf, sizeof(buf));
      totalRead.fetch_add(n);
      if (n > 0) {
        // 验证数据完整性
        for (size_t i = 0; i < n; ++i) {
          ASSERT_EQ(buf[i], 0xAB);
        }
      }
    }
  });

  producer.join();
  consumer.join();

  EXPECT_EQ(totalRead.load(), totalWritten.load());
  EXPECT_EQ(rb.available(), 0);
}

} // namespace test
} // namespace player
