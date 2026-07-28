/// @file clock_manager.cpp
/// @brief ClockManager — 三时钟 + Master 选择.

#include "core/clock/clock_manager.h"

namespace player {

ClockManager::ClockManager() = default;

// ── Master ──────────────────────────────────────────────────────────────

const Clock* ClockManager::getMaster() const {
  switch (m_masterSource) {
    case MasterClockSource::System:   return &m_systemClock;
    case MasterClockSource::External: return &m_externalClock;
    case MasterClockSource::Audio:
    default:                          return &m_audioClock;
  }
}

void ClockManager::setMasterSource(MasterClockSource source) {
  m_masterSource = source;
}

double ClockManager::masterTime() const {
  return getMaster()->getClock();
}

// ── 全局控制 ────────────────────────────────────────────────────────────

void ClockManager::setPaused(bool paused) {
  m_audioClock.setPaused(paused);
  m_systemClock.setPaused(paused);
  m_externalClock.setPaused(paused);
}

void ClockManager::setSpeed(double speed) {
  m_audioClock.setSpeed(speed);
  m_systemClock.setSpeed(speed);
  m_externalClock.setSpeed(speed);
}

} // namespace player
