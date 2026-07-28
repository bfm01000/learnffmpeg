/// @file test/unit/test_clock.cpp
/// @brief Clock + ClockManager 单元测试.

#include "core/clock/clock.h"
#include "core/clock/clock_manager.h"

#include <gtest/gtest.h>
#include <chrono>
#include <thread>

namespace player {
namespace test {

// ── Clock ────────────────────────────────────────────────────────────────

TEST(ClockTest, InitialValueNearZero) {
  Clock c;
  double t = c.getClock();
  EXPECT_GE(t, 0.0);
  EXPECT_LT(t, 1.0);   // 初始化后应该是很小的值
}

TEST(ClockTest, SetAndGet) {
  Clock c;
  c.setClock(10.5);
  EXPECT_NEAR(c.getClock(), 10.5, 0.1);
}

TEST(ClockTest, TimeAdvancesWhenPlaying) {
  Clock c;
  c.setClock(0.0);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  double t = c.getClock();
  EXPECT_GT(t, 0.05);   // 至少过了 50ms
  EXPECT_LT(t, 0.3);    // 不会超过 300ms
}

TEST(ClockTest, PausedDoesNotAdvance) {
  Clock c;
  c.setClock(1.0);
  c.setPaused(true);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  EXPECT_NEAR(c.getClock(), 1.0, 0.01);
}

TEST(ClockTest, ResumeFromPause) {
  Clock c;
  c.setClock(2.0);
  c.setPaused(true);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  c.setPaused(false);

  // 恢复后时间从 2.0 继续走
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_GT(c.getClock(), 2.02);
}

TEST(ClockTest, DoubleSpeed) {
  Clock c;
  c.setClock(0.0);
  c.setSpeed(2.0);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  double t = c.getClock();
  // 2x 速度, 100ms 挂钟 → 约 200ms 时钟
  EXPECT_GT(t, 0.15);
  EXPECT_LT(t, 0.35);
}

TEST(ClockTest, HalfSpeed) {
  Clock c;
  c.setClock(0.0);
  c.setSpeed(0.5);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  double t = c.getClock();
  // 0.5x 速度, 100ms 挂钟 → 约 50ms 时钟
  EXPECT_GT(t, 0.025);
  EXPECT_LT(t, 0.1);
}

TEST(ClockTest, SerialDefaultZero) {
  Clock c;
  EXPECT_EQ(c.getSerial(), 0);
}

TEST(ClockTest, SerialSetAndGet) {
  Clock c;
  c.setSerial(42);
  EXPECT_EQ(c.getSerial(), 42);
}

TEST(ClockTest, SetSpeedPreservesPts) {
  Clock c;
  c.setClock(5.0);
  double before = c.getClock();
  c.setSpeed(2.0);
  double after = c.getClock();
  // 变速不应导致 PTS 跳变（允许微小浮动）
  EXPECT_NEAR(before, after, 0.01);
}

// ── ClockManager ─────────────────────────────────────────────────────────

TEST(ClockManagerTest, DefaultMasterIsAudio) {
  ClockManager mgr;
  EXPECT_EQ(mgr.getMasterSource(), MasterClockSource::Audio);
  EXPECT_EQ(mgr.getMaster(), mgr.audioClock());
}

TEST(ClockManagerTest, SetMasterToSystem) {
  ClockManager mgr;
  mgr.setMasterSource(MasterClockSource::System);
  EXPECT_EQ(mgr.getMaster(), mgr.systemClock());
}

TEST(ClockManagerTest, SetMasterToExternal) {
  ClockManager mgr;
  mgr.setMasterSource(MasterClockSource::External);
  EXPECT_EQ(mgr.getMaster(), mgr.externalClock());
}

TEST(ClockManagerTest, SetPausedPropagates) {
  ClockManager mgr;
  mgr.setPaused(true);
  EXPECT_TRUE(mgr.audioClock()->isPaused());
  EXPECT_TRUE(mgr.systemClock()->isPaused());
  EXPECT_TRUE(mgr.externalClock()->isPaused());

  mgr.setPaused(false);
  EXPECT_FALSE(mgr.audioClock()->isPaused());
}

TEST(ClockManagerTest, SetSpeedPropagates) {
  ClockManager mgr;
  mgr.setSpeed(2.0);
  EXPECT_DOUBLE_EQ(mgr.audioClock()->getSpeed(), 2.0);
  EXPECT_DOUBLE_EQ(mgr.systemClock()->getSpeed(), 2.0);
  EXPECT_DOUBLE_EQ(mgr.externalClock()->getSpeed(), 2.0);
}

TEST(ClockManagerTest, MasterTime) {
  ClockManager mgr;
  mgr.audioClock()->setClock(3.0);
  EXPECT_NEAR(mgr.masterTime(), 3.0, 0.1);

  mgr.setMasterSource(MasterClockSource::System);
  mgr.systemClock()->setClock(7.0);
  EXPECT_NEAR(mgr.masterTime(), 7.0, 0.1);
}

TEST(ClockManagerTest, AudioClockIsNotSystemClock) {
  ClockManager mgr;
  // 验证三个是独立实例
  mgr.audioClock()->setClock(1.0);
  mgr.systemClock()->setClock(2.0);
  EXPECT_NEAR(mgr.audioClock()->getClock(), 1.0, 0.1);
  EXPECT_NEAR(mgr.systemClock()->getClock(), 2.0, 0.1);
}

} // namespace test
} // namespace player
