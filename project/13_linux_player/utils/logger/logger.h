#pragma once

#include <cstdarg>
#include <cstdio>
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
class Logger {
public:
    /// @brief 获取全局单例
    static Logger& instance();

    // ── 配置 ──────────────────────────────────────────────────────────────

    /// @brief 设置日志级别（低于该级别的消息将被过滤）
    void setLevel(LogLevel level);

    /// @brief 获取当前日志级别
    LogLevel getLevel() const { return level_; }

    /// @brief 设置日志输出文件（默认 stdout）
    void setOutputFile(const std::string& path);

    // ── 日志接口 ──────────────────────────────────────────────────────────

    /// @brief 格式化写日志
    void log(LogLevel level, const char* file, int line, const char* fmt, ...);

    /// @brief vprintf 风格的日志
    void vlog(LogLevel level, const char* file, int line, const char* fmt, va_list args);

    // ── 工具 ──────────────────────────────────────────────────────────────

    /// @brief 将级别转为字符串
    static const char* levelToString(LogLevel level);

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    LogLevel   level_;
    FILE*      output_;
    std::mutex mutex_;
};

// ── 便捷宏 ──────────────────────────────────────────────────────────────────

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
