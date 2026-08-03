/// Logger — spdlog-based logging with channel support.
/// Singleton pattern (pending removal per CLAUDE.md, kept for backward compat).

#include "logger.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace player {

namespace {
    std::shared_ptr<spdlog::logger> g_logger;
    std::mutex g_mutex;
    std::map<std::string, std::shared_ptr<spdlog::logger>> g_tagLoggers;
} // anonymous namespace

Logger& Logger::instance()
{
    static Logger inst;
    return inst;
}

Logger::Logger()
    : level_(LogLevel::Info)
{
    // Create default stderr logger
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_logger) {
        g_logger = spdlog::stderr_color_mt("player");
        g_logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
        g_logger->set_level(spdlog::level::info);
        g_logger->flush_on(spdlog::level::debug);
    }
}

Logger::~Logger()
{
    spdlog::drop_all();
}

void Logger::setLevel(LogLevel level)
{
    level_ = level;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_logger) {
        g_logger->set_level(static_cast<spdlog::level::level_enum>(level));
    }
}

void Logger::setOutputFile(const std::string& path)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    try {
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, true);
        g_logger = std::make_shared<spdlog::logger>("player", sink);
        g_logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
        g_logger->set_level(static_cast<spdlog::level::level_enum>(level_));
        g_logger->flush_on(spdlog::level::debug);
    } catch (...) {
        // Fallback to stderr
        g_logger = spdlog::stderr_color_mt("player_fallback");
    }
}

void Logger::setTagLevel(const std::string& tag, LogLevel level)
{
    std::lock_guard<std::mutex> lock(mutex_);
    tagLevels_[tag] = level;
}

bool Logger::isEnabled(const char* tag, LogLevel level) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tagLevels_.find(tag);
    if (it != tagLevels_.end()) {
        return level >= it->second;
    }
    return level >= level_;
}

void Logger::log(LogLevel level, const char* file, int line, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vlog(nullptr, level, file, line, fmt, args);
    va_end(args);
}

void Logger::log(const char* tag, LogLevel level, const char* file, int line,
                 const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vlog(tag, level, file, line, fmt, args);
    va_end(args);
}

void Logger::vlog(const char* tag, LogLevel level, const char* file, int line,
                  const char* fmt, va_list args)
{
    // Level check
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tag) {
            auto it = tagLevels_.find(tag);
            if (it != tagLevels_.end() && level < it->second) return;
        }
        if (level < level_) return;
    }

    // Format message with optional tag prefix
    char buf[4096];
    int off = 0;
    if (tag) {
        const char* fname = strrchr(file, '/');
        fname = fname ? fname + 1 : file;
        off = snprintf(buf, sizeof(buf), "[%-6s] %s:%-4d ", tag, fname, line);
    }
    vsnprintf(buf + off, sizeof(buf) - off, fmt, args);

    // Log via spdlog
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_logger) {
        g_logger->log(static_cast<spdlog::level::level_enum>(level), "{}", buf);
    }
}

const char* Logger::levelToString(LogLevel level)
{
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::None:  return "NONE";
        default:              return "UNKNOWN";
    }
}

} // namespace player
