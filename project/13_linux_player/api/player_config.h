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
// =============================================================================
// PlayerConfig — 配置树设计说明
// =============================================================================
//
// ## 1. 这是什么？
//
// PlayerConfig 是一个「嵌套结构体配置树」(Nested-Struct Configuration Tree)。
// 整个播放器的全部可配置项被按子系统分组，嵌套在子 struct 中，形成一棵树：
//
//   PlayerConfig
//   ├── Source        (媒体源: 超时、重连、探测大小……)
//   ├── Decode        (解码: HW加速、线程数、容错……)
//   ├── Render
//   │   ├── Video     (视频渲染: 驱动、VSync、色彩空间……)
//   │   └── Audio     (音频渲染: 驱动、采样率、延迟……)
//   ├── Sync          (音视频同步: 主时钟源、丢帧阈值……)
//   ├── Playlist      (播放列表: 循环、无缝切换……)
//   ├── Log           (日志: 级别、输出目标……)
//   └── Misc          (队列容量、看门狗……)
//
// 每个叶子字段都有默认值（C++17 成员默认初始化），用户只需要覆盖关心的部分。
//
//
// ## 2. 为什么这么设计？
//
// ### 2.1 不使用 AVDictionary (FFmpeg 风格)
//
// FFmpeg 内部大量使用 AVDictionary（字符串 key-value 对），例如：
//   av_dict_set(&opts, "timeout", "5000000", 0);
//
// 问题:
//   - 无类型安全: "5000000" 还是 "5_000_000"？编译期无法检查。
//   - 无 IDE 补全: 开发者必须记住或查阅 key 的字符串名。
//   - key 拼写错误只在运行时暴露。
//   - 不表达结构: 所有选项是扁平的一维表，"video.vsync" 和 "audio.latency_ms"
//     之间的关系完全靠命名约定，没有编译器保证。
//
// ### 2.2 不使用 JSON / YAML / INI 文件直接反序列化
//
// 问题:
//   - 引入第三方解析库依赖。
//   - 序列化/反序列化有性能开销（虽然不大，但播放器启动路径应尽量轻量）。
//   - 配置文件的 schema 需要单独维护和文档化。
//   - 运行时错误: 缺少字段、类型错误在解析时才发现。
//
// 本方案的做法: PlayerConfig 是 C++ 原生 struct，如果你需要从 JSON/YAML 读配置，
// 只需在外部做一次映射(JSON → PlayerConfig)，核心 SDK 不依赖任何序列化格式。
//
// ### 2.3 嵌套 struct 的优势
//
// (a) 命名空间隔离 (Namespace Isolation)
//     Render.Video.vsync   vs  flat: video_vsync
//     Render.Audio.latency_ms vs  flat: audio_latency_ms
//     嵌套结构天然避免了 "video_", "audio_" 前缀，且不会出现 100 个 flat 字段
//     挤在一个 struct 里。
//
// (b) IDE 自文档化 (IDE Self-Documenting)
//     用户在 IDE 中输入 `config.render.video.` 会自动列出所有视频渲染选项。
//     不需要翻文档、不需要查 wiki。
//
// (c) 部分配置传递 (Partial Config Passing)
//     如果你只想初始化视频渲染器，你可以只传递 `config.render.video`。
//     这意味着模块只看到自己需要的配置，不会看到整个全局配置 —
//     符合「最小权限原则」(Principle of Least Privilege)。
//
// (d) 编译期安全 (Compile-Time Safety)
//     拼写错误（vysnc vs vsync）、类型错误（int 赋给 bool）在编译期就报错。
//
// (e) 易于扩展 (Easy Extension)
//     在 Decode 下新增一个字段，不影响 Source、Render、Sync 等任何其他模块。
//     PR diff 最小化，review 容易。
//
// (f) 通俗易懂 (Trivial Copy Semantics)
//     PlayerConfig 是 trivial 的 POD 结构体，拷贝即深拷贝，不需要 clone()。
//     线程间传递配置只需 `auto cfg = player_config_;` 一把拷贝，无锁、无生命周期问题。
//
// ### 2.4 Preset (预设) 模式
//
// livePreset() / localFilePreset() 是静态工厂方法，返回一个预填好的配置对象。
// 这是「约定大于配置」(Convention over Configuration) 的具体体现：
//   - 90% 的用户不需要理解每个字段的含义，只需选一个预设。
//   - 高级用户可以 `auto c = livePreset(); c.sync.master_clock = Audio;` 在预设上微调。
//
// 对比: 有些播放器把「直播模式」「文件模式」做成 if-else 散落在各个模块里。
//       预设模式让这种差异集中在一处，逻辑清晰、可测试。
//
//
// ## 3. 主流大厂是怎么做的？
//
// ### 3.1 VLC / libVLC
// VLC 使用模块化配置系统，本质上是全局字符串 key-value 表 + 类型标记。
//   var_GetInteger(p_mi, "volume")
//   var_SetString(p_mi, "avcodec-hw", "vaapi")
// 优点: 极其灵活，支持运行时 GUI 编辑、命令行覆盖、配置文件持久化。
// 缺点: 字符串型，非类型安全；模块间的配置耦合在全局 namespace 里；性能开销。
// 评价: 适合「用户可自由配置一切」的场景，但对 SDK 而言过重。
//
// ### 3.2 mpv
// mpv 使用 `m_option` 系统：每个选项是一个带类型的结构体 + 全局注册表。
// 所有选项以 "." 分隔的扁平路径存在（如 `--vo=gpu`、`--audio-samplerate=44100`）。
// 优点: 极其强大，支持 CLI、配置文件、运行时属性页、C plugin API。
// 缺点: 选项系统本身的代码量巨大（数千行），学习曲线陡峭。
// 评价: 适合最终播放器应用，不适合作为嵌入式 SDK 的配置方案。
//
// ### 3.3 GStreamer
// GStreamer 使用 GObject 属性系统: 每个 element 通过 `g_object_set()` 设置属性。
//   g_object_set(G_OBJECT(sink), "sync", TRUE, NULL);
// 优点: 与 GObject 类型系统深度集成，支持 introspection（动态语言绑定）。
// 缺点: 极度依赖 GLib 生态；字符串 key 而非编译期检查；GObject 本身很重。
// 评价: GStreamer 选择了与 GLib 一致的风格，但这是 C 生态的约束，不是设计选择。
//
// ### 3.4 ExoPlayer (Android) / AVPlayer (iOS)
// Android 的 ExoPlayer 使用 Builder 模式:
//   new ExoPlayer.Builder(context)
//       .setMediaSourceFactory(...)
//       .setLoadControl(...)
//       .build();
// 每个 Builder 内部是嵌套的配置对象。
// AVPlayer 更简洁: 大部分配置通过 AVMutableAudioMix 等独立对象注入。
// 评价: 移动端 SDK 倾向于 Builder/Fluent API + 合理默认值。
//       本项目 Preset 模式本质上是 Builder 的一种 C++ 等价表达。
//
// ### 3.5 国内商业播放器 SDK (如阿里云、腾讯云、七牛、金山)
// 大部分采用「配置 struct + 默认值 + JSON 反序列化」混合方案：
//   - C/C++ SDK 层: 一个巨大的配置 struct，所有字段有默认值。
//   - 上层封装: JSON 字符串 → struct 字段映射（通常是代码生成或手写映射）。
//   这种方案和我们当前的设计高度一致。
//
// ### 3.6 Chromium / Cobalt (YouTube HTML5 播放器底层)
// Chromium 的 media 层使用嵌套 struct 配置:
//   media::AudioConfig, media::VideoConfig, media::PipelineConfig
// 每个 config 有合理的默认值，外部通过修改 struct 字段来定制。
// 和我们当前方案几乎一样。
//
//
// ## 4. 结论
//
// 本文件的配置树设计:
//
//   ✅ 是 C++ 播放器 SDK 的标准做法（Chromium、商业 SDK 皆然）。
//   ✅ 避免了 FFmpeg/VLC 字符串 key 的类型不安全问题。
//   ✅ 嵌套 struct 提供命名空间隔离、IDE 自文档化、部分配置传递。
//   ✅ 预设模式让简单场景只需一行代码。
//   ✅ 不依赖任何序列化框架 —— 如果你需要 JSON/YAML，只需在外层做一次映射。
//   ✅ 扩展一个字段只需加一行成员 + 默认值，diff 最小化。
//
//   ❌ 不适合需要运行时通过字符串 key 动态修改配置的场景（如 GUI 属性编辑器）。
//      如果需要这种能力，应该在 PlayerConfig 之上再加一层反射/映射层，
//      而不是替换掉 PlayerConfig。
//
// =============================================================================