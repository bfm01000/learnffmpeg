#pragma once

#include <cstdarg>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>

namespace player {

/// @brief 日志级别
enum class LogLevel {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    None  = 5,  ///< 关闭所有日志
};

/// @brief 日志器 —— 单例
///
/// 线程安全。支持级别过滤和格式化输出。
/// 默认输出到 stderr，可通过 setOutputFile() 切换。
class Logger {
public:
    /// @brief 获取全局单例
    static Logger& instance();

    // ── 配置 ──────────────────────────────────────────────────────────────

    /// @brief 设置日志级别（低于该级别的消息将被过滤）
    void setLevel(LogLevel level);

    /// @brief 获取当前日志级别
    LogLevel getLevel() const { return level_; }

    /// @brief 设置日志输出文件（默认 stderr）
    void setOutputFile(const std::string& path);

    /// @brief 设置指定 tag 的级别（空 tag = 全局级别）.
    ///        设置后只有该 tag 的日志受此级别约束，其他 tag 仍用全局级别.
    void setTagLevel(const std::string& tag, LogLevel level);

    // ── 日志接口 ──────────────────────────────────────────────────────────

    /// @brief 带 tag 的格式化日志
    void log(const char* tag, LogLevel level, const char* file, int line,
             const char* fmt, ...);

    /// @brief 无 tag 的格式化日志（使用全局级别）
    void log(LogLevel level, const char* file, int line, const char* fmt, ...);

    /// @brief vprintf 风格的日志
    void vlog(const char* tag, LogLevel level, const char* file, int line,
              const char* fmt, va_list args);

    // ── 工具 ──────────────────────────────────────────────────────────────

    static const char* levelToString(LogLevel level);

    /// @brief 检查指定 tag+level 是否会被输出
    bool isEnabled(const char* tag, LogLevel level) const;

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    LogLevel   level_;
    FILE*      output_;
    mutable std::mutex mutex_;
    std::map<std::string, LogLevel> tagLevels_; // per-tag level overrides
};

// ── 通道化日志宏（用于 AV 同步调试）────────────────────────────────────────
// 使用方式: LOGD_AV("frame pts=%.3f clock=%.3f", fPts, clk);
// 输出格式: [HH:MM:SS.mmm] [avsync] [DEBUG] file.cpp:123 frame pts=0.04s clock=0.05s
// 运行时可过滤: Logger::instance().setTagLevel("avsync", LogLevel::Debug);

#define LOG_TAG(level, tag, fmt, ...) \
    player::Logger::instance().log(tag, level, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOGD_TAG(tag, fmt, ...) \
    LOG_TAG(player::LogLevel::Debug, tag, fmt, ##__VA_ARGS__)

// AV sync channel — 音视频同步决策
#define LOGD_AV(fmt, ...)     LOGD_TAG("avsync", fmt, ##__VA_ARGS__)
// Audio channel — 音频解码、时钟更新
#define LOGD_AUDIO(fmt, ...)  LOGD_TAG("audio",  fmt, ##__VA_ARGS__)
// Video channel — 视频解码、队列
#define LOGD_VIDEO(fmt, ...)  LOGD_TAG("video",  fmt, ##__VA_ARGS__)
// Clock channel — 时钟值变化
#define LOGD_CLOCK(fmt, ...)  LOGD_TAG("clock",  fmt, ##__VA_ARGS__)
// Render channel — pumpEvents 渲染流程
#define LOGD_RENDER(fmt, ...) LOGD_TAG("render", fmt, ##__VA_ARGS__)

// ── 通用便捷宏 ────────────────────────────────────────────────────────────

#define LOG_TRACE(fmt, ...) \
    player::Logger::instance().log(player::LogLevel::Trace, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_DEBUG(fmt, ...) \
    player::Logger::instance().log(player::LogLevel::Debug, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) \
    player::Logger::instance().log(player::LogLevel::Info, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    player::Logger::instance().log(player::LogLevel::Warn, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    player::Logger::instance().log(player::LogLevel::Error, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

} // namespace player
