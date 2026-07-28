#pragma once

#include <cstdint>
#include <string>

namespace player {

/// @brief 播放器状态枚举
enum class PlayerState {
  Idle,        // 初始 / 已停止
  Loading,     // open() 后，解析媒体中
  Ready,       // 首帧就绪，等待 play()
  Playing,     // 播放中
  Paused,      // 暂停
  Buffering,   // 缓冲中（underrun）
  Completed,   // 播放完成（EOS）
  Error,       // 错误
  Stopping,    // 停止中（等线程退出）
};

/// @brief 媒体类型
enum class MediaType {
  Unknown  = -1,
  Video    = 0,
  Audio    = 1,
  Subtitle = 2,
};

/// @brief 错误码
enum class ErrorCode : int32_t {
  Ok                = 0,
  Unknown           = -1,
  InvalidArg        = -2,
  OutOfMemory       = -3,
  OpenFailed        = -100,
  ProtocolNotSupport= -101,
  StreamNotFound    = -102,
  DecodeError       = -200,
  DecodeHWFallback  = -201,
  RenderError       = -300,
  NetworkTimeout    = -400,
  NetworkDisconnect = -401,
  InternalFatal     = -999,
};

/// @brief 视频渲染模式
enum class VideoRenderMode {
  Auto,        // 自动选择最优
  OpenGL,      // OpenGL / GLFW 窗口
  Offscreen,   // 离屏渲染（获取帧回调）
  None,        // 不渲染视频
};

/// @brief 音频渲染模式
enum class AudioRenderMode {
  Auto,        // 自动选择最优
  SDL2,        // SDL2 音频
  Offscreen,   // 离屏（获取 PCM 回调）
  None,        // 不渲染音频
};

/// @brief HW 加速后端
enum class HWAccelBackend {
  None,      // 软解
  VAAPI,     // Intel / AMD
  VDPAU,     // NVIDIA legacy
  CUDA,      // NVIDIA
  Auto,      // 自动选择最优
};

/// @brief 主时钟源
enum class MasterClockSource {
  Audio,     // 以音频时钟为 Master（默认）
  System,    // 系统挂钟（纯视频场景）
  External,  // 外部注入时钟（直播场景）
};

/// @brief Seek 标志
enum class SeekFlag {
  Default     = 0,
  Accurate    = 1 << 0,  // 精确 seek（解码到目标帧）
  Fast        = 1 << 1,  // 快速 seek（到最近的关键帧）
  Any         = 1 << 2,  // 任意帧（即使非关键帧）
};

} // namespace player
