#pragma once

/// @file clock.h
/// @brief 播放器时钟。追踪 PTS, 支持暂停和变速。
///
/// ==========================================================================
/// 原理
/// ==========================================================================
///   setClock(pts):  记录 pts 值和当时挂钟时间。
///   getClock():    = m_pts + (now - m_lastUpdated) * m_speed  (播放中)
///                  = m_pts                                     (暂停时)
///   setPaused(true):  冻结 m_pts 为当前值, 恢复时 m_lastUpdated = now.
///   setSpeed(2.0):    2 倍速播放, getClock() 时间流逝速度翻倍.
///
/// ==========================================================================
/// 为什么不用 std::chrono 做原子操作
/// ==========================================================================
///   用 double 秒数替代 time_point 作为原子变量, 避免 trivially copyable 限制,
///   同时保持亚微秒精度（double 的 53 位尾数在 1 小时内仍有 ~0.1ns 分辨率）。

#include <atomic>
#include <cstdint>

namespace player {

class Clock {
public:
  Clock();

  /// 设置时钟值（秒）。通常在 Audio 回调中调用, 或 Seek 后重置.
  void setClock(double ptsSeconds);

  /// 获取当前时钟值（秒）。播放中 = m_pts + 流逝时间 * 速度, 暂停时 = 冻结值.
  double getClock() const;

  /// 设置为暂停/恢复
  void setPaused(bool paused);
  bool isPaused() const { return m_paused.load(std::memory_order_acquire); }

  /// 播放速度 (1.0 = 正常, 0.5 = 半速, 2.0 = 2x)
  void   setSpeed(double speed);
  double getSpeed() const { return m_speed.load(std::memory_order_acquire); }

  /// 序列号 — Seek 时递增, 配合 PacketQueue::serial() 检测 stale 数据.
  void    setSerial(int serial);
  int     getSerial() const { return m_serial.load(std::memory_order_acquire); }

private:
  static double nowSeconds_();

  std::atomic<double>  m_pts{0.0};
  std::atomic<double>  m_lastUpdated{0.0};  // 最后一次 setClock 时的挂钟秒数
  std::atomic<double>  m_speed{1.0};
  std::atomic<bool>    m_paused{false};
  std::atomic<int>     m_serial{0};
};

} // namespace player
