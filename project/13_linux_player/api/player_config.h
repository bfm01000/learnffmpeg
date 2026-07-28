#pragma once

#include "player_types.h"

#include <cstdint>
#include <string>

namespace player {

/// @brief 配置树，可逐层覆盖默认值
struct PlayerConfig {
  // ── Source ──────────────────────────────────────────────────────────────
  struct Source {
    int32_t timeout_ms       = 10000;          // 连接超时
    bool    reconnect        = true;           // 自动重连
    int32_t max_reconnect    = 3;              // 最大重连次数
    int32_t buffer_size      = 4 * 1024 * 1024; // 网络缓冲 (bytes)
    int64_t probesize        = 5 * 1024 * 1024; // 分析大小
    int64_t max_analyze_duration = 5 * AV_TIME_BASE_Q; // 分析时长
    bool    enable_low_latency = false;        // 低延迟模式（直播用）
    std::string user_agent   = "LinuxPlayerSDK/0.1";
    std::string referer;
    std::string headers;                       // 自定义 HTTP headers (CRLF)
  } source;

  // ── Decode ──────────────────────────────────────────────────────────────
  struct Decode {
    HWAccelBackend hw_accel      = HWAccelBackend::Auto;
    int32_t video_thread_count   = 0;          // 0 = auto
    int32_t skip_loop_filter     = 0;          // H.264: 0=all, 8=none
    int32_t max_consecutive_err  = 100;        // 连续解码错误上限
    bool    enable_low_delay     = false;      // ffmpeg low_delay flag
    int32_t refcounted_frames    = 1;          // AVCodecContext refcounted_frames
    std::string dec_name_override;            // 强制指定解码器名
  } decode;

  // ── Render ──────────────────────────────────────────────────────────────
  struct Render {
    struct Video {
      VideoRenderMode driver    = VideoRenderMode::Auto;
      bool    vsync             = true;
      int32_t color_space       = 1;           // 0=BT.601, 1=BT.709, 2=BT.2020
      bool    deinterlace       = false;
      bool    fullscreen        = false;
      int32_t window_width      = 0;           // 0 = 按视频分辨率
      int32_t window_height     = 0;
      std::string window_title  = "Player SDK";
      std::string shader_dir;                  // custom shader directory
    } video;

    struct Audio {
      AudioRenderMode driver    = AudioRenderMode::Auto;
      int32_t sample_rate       = 44100;
      int32_t channels          = 2;
      int32_t format            = 1;           // AV_SAMPLE_FMT_S16
      int32_t latency_ms        = 50;
      int32_t buffer_samples    = 2048;
    } audio;
  } render;

  // ── Sync ────────────────────────────────────────────────────────────────
  struct Sync {
    MasterClockSource master_clock = MasterClockSource::Audio;
    int64_t max_frame_delay_us     = 100000;   // 100ms 最大等待
    int64_t drop_threshold_us      = -100000;  // -100ms 触发丢帧
    int64_t sync_tolerance_us      = 10000;    // 10ms 同步容差
    double  play_speed             = 1.0;
    bool    enable_frame_step      = false;
  } sync;

  // ── Playlist ────────────────────────────────────────────────────────────
  struct Playlist {
    bool    enable         = false;
    int32_t loop           = 0;               // 0=no loop, -1=infinite, >0=count
    bool    gapless        = false;           // 无缝切换
    int64_t preload_next_ms= 3000;            // 提前预加载下一首
  } playlist;

  // ── Log ─────────────────────────────────────────────────────────────────
  struct Log {
    std::string level      = "info";          // trace/debug/info/warn/error
    std::string output     = "stdout";        // stdout / file / syslog
    std::string file_path;
    bool    enable_trace   = false;           // 详细的线程/帧追踪
  } log;

  // ── Misc ────────────────────────────────────────────────────────────────
  struct Misc {
    int32_t video_pkt_q_size = 256;           // 视频 PacketQueue 容量
    int32_t audio_pkt_q_size = 512;           // 音频 PacketQueue 容量
    int32_t video_frm_q_size = 5;             // 视频 FrameQueue 容量
    int32_t audio_frm_q_size = 16;            // 音频 FrameQueue 容量
    bool    enable_watchdog   = false;        // 看门狗线程
    int32_t watchdog_interval_ms = 3000;
    std::string plugin_dir;                   // .so 插件扫描目录
  } misc;

  // ── 便捷构造 ────────────────────────────────────────────────────────────

  /// 低延迟直播预设
  static PlayerConfig livePreset() {
    PlayerConfig c;
    c.source.timeout_ms      = 5000;
    c.source.buffer_size     = 512 * 1024;
    c.source.enable_low_latency = true;
    c.decode.enable_low_delay   = true;
    c.decode.skip_loop_filter   = 8;
    c.misc.audio_pkt_q_size     = 256;
    c.misc.video_pkt_q_size     = 128;
    c.sync.master_clock         = MasterClockSource::External;
    c.render.audio.latency_ms   = 30;
    return c;
  }

  /// 本地播放预设
  static PlayerConfig localFilePreset() {
    PlayerConfig c;
    c.source.reconnect = false;
    c.decode.hw_accel  = HWAccelBackend::VAAPI;
    c.render.video.vsync = true;
    c.sync.master_clock   = MasterClockSource::Audio;
    return c;
  }

private:
  static constexpr int64_t AV_TIME_BASE_Q = 1000000; // AV_TIME_BASE in us
};

} // namespace player
