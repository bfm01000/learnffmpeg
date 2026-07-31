#include "logger.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <map>
#include <sstream>

namespace player {

Logger& Logger::instance()
{
    static Logger inst;
    return inst;
}

Logger::Logger()
    : level_(LogLevel::Info)
    , output_(stderr)  // default to stderr for debugging
{
}

Logger::~Logger()
{
    if (output_ && output_ != stdout && output_ != stderr) {
        std::fclose(output_);
    }
}

void Logger::setLevel(LogLevel level)
{
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

void Logger::setOutputFile(const std::string& path)
{
    std::lock_guard<std::mutex> lock(mutex_);

    FILE* new_output = std::fopen(path.c_str(), "a");
    if (!new_output) {
        new_output = stderr;
    }

    if (output_ && output_ != stdout && output_ != stderr) {
        std::fclose(output_);
    }
    output_ = new_output;
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
    // Level check: per-tag first, then global
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tag) {
            auto it = tagLevels_.find(tag);
            if (it != tagLevels_.end()) {
                if (level < it->second) return;
            } else if (level < level_) {
                return;
            }
        } else if (level < level_) {
            return;
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Timestamp
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;

    struct tm tm_buf;
    localtime_r(&now_c, &tm_buf);

    // Format: [HH:MM:SS.mmm] [TAG] [LVL] file:line msg
    if (tag) {
        // Extract just the filename from the full path
        const char* fname = strrchr(file, '/');
        fname = fname ? fname + 1 : file;

        std::fprintf(output_, "[%02d:%02d:%02d.%03lld] [%-6s] [%s] %s:%d ",
            tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
            static_cast<long long>(ms.count()),
            tag, levelToString(level), fname, line);
    } else {
        std::fprintf(output_, "[%02d:%02d:%02d.%03lld] [%s] %s:%d - ",
            tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
            static_cast<long long>(ms.count()),
            levelToString(level), file, line);
    }

    std::vfprintf(output_, fmt, args);
    std::fprintf(output_, "\n");
    std::fflush(output_);
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
