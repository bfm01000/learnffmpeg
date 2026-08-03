#pragma once

#include "player_config.h"
#include "player_callback.h"
#include "player_types.h"
#include "result.h"

#include <cstdint>
#include <memory>
#include <string>

namespace player {

/// @brief 播放器主接口（Facade）
///
/// 线程安全。所有 public 方法可从任意线程调用。
/// 回调通过 IPlayerCallback 在内部 Event 线程上异步通知。
class IPlayer {
public:
  virtual ~IPlayer() = default;

  // ── 生命周期 ──────────────────────────────────────────────────────────

  /// 打开媒体资源（本地文件 / 网络流）
  virtual Result<void> open(const char* url) = 0;

  /// 带配置打开
  virtual Result<void> open(const char* url, const PlayerConfig& config) = 0;

  // ── 播放控制 ──────────────────────────────────────────────────────────

  virtual Result<void> play()             = 0;
  virtual Result<void> pause()            = 0;
  virtual Result<void> stop()             = 0;

  /// Seek 到指定位置（毫秒）
  virtual Result<void> seek(int64_t position_ms) = 0;

  /// 设置播放速度 (0.5x ~ 2.0x)
  virtual Result<void> setSpeed(double speed) = 0;

  /// 设置音量 (0.0 ~ 1.0)
  virtual Result<void> setVolume(float volume) = 0;

  /// 循环播放
  virtual Result<void> setLoop(bool loop) = 0;

  // ── 事件泵 ────────────────────────────────────────────────────────────

  /// 主线程事件泵 — 调用者需在帧循环中周期性调用此方法.
  /// 处理 SDL 事件轮询 + 视频帧渲染 (必须在主线程调用, X11 要求).
  /// @return true 窗口仍然打开, false 窗口已关闭
  virtual bool pumpEvents() = 0;

  // ── 查询 ──────────────────────────────────────────────────────────────

  virtual PlayerState  getState()    const = 0;
  virtual Result<int64_t> getPosition() const = 0;   // ms
  virtual Result<int64_t> getDuration() const = 0;   // ms
  virtual bool         isPlaying()   const = 0;
  virtual bool         isSeeking()   const = 0;

  // ── 回调 ──────────────────────────────────────────────────────────────

  virtual void setCallback(IPlayerCallback* callback) = 0;

  // ── 工厂 ──────────────────────────────────────────────────────────────

  static std::unique_ptr<IPlayer> create();
  static std::unique_ptr<IPlayer> create(const PlayerConfig& config);
};

} // namespace player
