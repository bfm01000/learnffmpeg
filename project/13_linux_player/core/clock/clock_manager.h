#pragma once

/// @file clock_manager.h
/// @brief 三套时钟管理器。管理 Audio / System / External 时钟并选择 Master.
///
///   - Audio Clock:    音频渲染线程更新（SDL 回调中）。默认 Master.
///   - System Clock:   系统挂钟。纯视频播放场景下使用.
///   - External Clock: 外部注入。直播流等场景下由网络模块更新.

#include "api/player_types.h"
#include "core/clock/clock.h"

namespace player {

class ClockManager {
public:
  ClockManager();

  // ── Master ────────────────────────────────────────────────────────────

  /// 获取当前主时钟（只读）
  const Clock* getMaster() const;

  /// 设置主时钟源
  void setMasterSource(MasterClockSource source);
  MasterClockSource getMasterSource() const { return m_masterSource; }

  // ── 各时钟访问 ────────────────────────────────────────────────────────

  Clock* audioClock()    { return &m_audioClock; }
  Clock* systemClock()   { return &m_systemClock; }
  Clock* externalClock() { return &m_externalClock; }

  const Clock* audioClock()    const { return &m_audioClock; }
  const Clock* systemClock()   const { return &m_systemClock; }
  const Clock* externalClock() const { return &m_externalClock; }

  /// 主时钟当前值（秒），便捷方法。
  double masterTime() const;

  /// 设置所有时钟的播放状态
  void setPaused(bool paused);
  void setSpeed(double speed);

private:
  Clock              m_audioClock;
  Clock              m_systemClock;
  Clock              m_externalClock;
  MasterClockSource  m_masterSource = MasterClockSource::Audio;
};

} // namespace player
