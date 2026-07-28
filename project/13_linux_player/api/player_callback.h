#pragma once

#include "player_types.h"

#include <cstdint>

namespace player {

/// @brief 播放器事件回调接口
///
/// 回调在内部 Event 线程上执行，不得阻塞。
/// 所有方法均有默认空实现——App 按需重写。
class IPlayerCallback {
public:
  virtual ~IPlayerCallback() = default;

  // ── 生命周期 ──────────────────────────────────────────────────────────

  /// 媒体加载完成，准备播放
  virtual void onPrepared(int64_t duration_ms) {}

  /// 播放开始
  virtual void onPlay() {}

  /// 播放恢复（从 Pause）
  virtual void onResume() {}

  /// 播放暂停
  virtual void onPause() {}

  /// 播放停止
  virtual void onStopped() {}

  /// 播放完成（EOS）
  virtual void onCompletion() {}

  // ── 进度 ──────────────────────────────────────────────────────────────

  /// 进度更新（约 500ms 一次）
  virtual void onProgress(int64_t position_ms, int64_t duration_ms) {}

  // ── Seek ──────────────────────────────────────────────────────────────

  virtual void onSeekComplete(int64_t position_ms) {}
  virtual void onSeekFailed(int64_t target_ms, ErrorCode error) {}

  // ── 缓冲 ──────────────────────────────────────────────────────────────

  virtual void onBufferingStart() {}
  virtual void onBufferingEnd() {}
  virtual void onBufferingProgress(int32_t percent) {}

  // ── 错误 ──────────────────────────────────────────────────────────────

  virtual void onError(ErrorCode code, const char* message) {}
  virtual void onWarning(ErrorCode code, const char* message) {}

  // ── 信息 ──────────────────────────────────────────────────────────────

  /// 流信息变更（如切换音轨）
  virtual void onStreamInfoChanged(int video_count, int audio_count, int sub_count) {}

  /// 当前播放的流索引
  virtual void onTrackChanged(MediaType type, int stream_index) {}

  // ── 状态 ──────────────────────────────────────────────────────────────

  virtual void onStateChanged(PlayerState old_state, PlayerState new_state) {}

  // ── 帧数据（仅 Offscreen 模式） ──────────────────────────────────────

  virtual void onVideoFrame(const uint8_t* data[4],
                            const int     linesize[4],
                            int           width,
                            int           height,
                            int           pix_fmt,
                            int64_t       pts_us) {}

  virtual void onAudioFrame(const uint8_t* data,
                            int           samples,
                            int           sample_rate,
                            int           channels,
                            int           format) {}
};

} // namespace player
