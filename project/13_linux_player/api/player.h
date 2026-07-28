#pragma once

#include "player_config.h"
#include "player_callback.h"
#include "player_types.h"

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
  /// @return 0 成功, <0 失败
  virtual int open(const char* url) = 0;

  /// 带配置打开
  virtual int open(const char* url, const PlayerConfig& config) = 0;

  // ── 播放控制 ──────────────────────────────────────────────────────────

  virtual int play()             = 0;
  virtual int pause()            = 0;
  virtual int stop()             = 0;

  /// Seek 到指定位置（毫秒）
  virtual int seek(int64_t position_ms) = 0;

  /// 设置播放速度 (0.5x ~ 2.0x)
  virtual int setSpeed(double speed) = 0;

  /// 设置音量 (0.0 ~ 1.0)
  virtual int setVolume(float volume) = 0;

  /// 循环播放
  virtual int setLoop(bool loop) = 0;

  // ── 查询 ──────────────────────────────────────────────────────────────

  virtual PlayerState getState()       const = 0;
  virtual int64_t     getPosition()    const = 0;   // ms
  virtual int64_t     getDuration()    const = 0;   // ms
  virtual bool        isPlaying()      const = 0;
  virtual bool        isSeeking()      const = 0;

  // ── 回调 ──────────────────────────────────────────────────────────────

  virtual void setCallback(IPlayerCallback* callback) = 0;

  // ── 工厂 ──────────────────────────────────────────────────────────────

  static std::unique_ptr<IPlayer> create();
  static std::unique_ptr<IPlayer> create(const PlayerConfig& config);
};

} // namespace player
