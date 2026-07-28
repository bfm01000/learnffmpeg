/// @file clock.cpp
/// @brief Clock — PTS 追踪器实现.

#include "core/clock/clock.h"

#include <chrono>

namespace player {

// ── 辅助 ────────────────────────────────────────────────────────────────

double Clock::nowSeconds_() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// ── 生命周期 ────────────────────────────────────────────────────────────

Clock::Clock() {
  m_lastUpdated.store(nowSeconds_(), std::memory_order_relaxed);
}

// ── 核心 ────────────────────────────────────────────────────────────────

void Clock::setClock(double ptsSeconds) {
  m_pts.store(ptsSeconds, std::memory_order_release);
  m_lastUpdated.store(nowSeconds_(), std::memory_order_release);
}

double Clock::getClock() const {
  if (m_paused.load(std::memory_order_acquire)) {
    return m_pts.load(std::memory_order_acquire);
  }
  double now   = nowSeconds_();
  double pts   = m_pts.load(std::memory_order_acquire);
  double last  = m_lastUpdated.load(std::memory_order_acquire);
  double speed = m_speed.load(std::memory_order_acquire);
  return pts + (now - last) * speed;
}

void Clock::setPaused(bool paused) {
  bool wasPaused = m_paused.exchange(paused, std::memory_order_acq_rel);
  if (wasPaused == paused) return;  // 状态未变

  if (paused) {
    // 冻结当前值
    m_pts.store(getClock(), std::memory_order_release);
  } else {
    // 恢复时重新对时
    m_lastUpdated.store(nowSeconds_(), std::memory_order_release);
  }
}

void Clock::setSpeed(double speed) {
  // 变速前先冻结当前值, 避免突变
  setPaused(true);
  m_speed.store(speed, std::memory_order_release);
  setPaused(false);
}

void Clock::setSerial(int serial) {
  m_serial.store(serial, std::memory_order_release);
}

} // namespace player
