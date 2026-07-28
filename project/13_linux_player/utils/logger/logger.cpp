#include "logger.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace player {

Logger& Logger::instance()
{
    static Logger inst;
    return inst;
}

Logger::Logger()
    : level_(LogLevel::Info)
    , output_(stdout)
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
        // Fallback to stderr on failure
        new_output = stderr;
    }

    if (output_ && output_ != stdout && output_ != stderr) {
        std::fclose(output_);
    }
    output_ = new_output;
}

void Logger::log(LogLevel level, const char* file, int line, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vlog(level, file, line, fmt, args);
    va_end(args);
}

void Logger::vlog(LogLevel level, const char* file, int line, const char* fmt, va_list args)
{
    if (level < level_) {
        return; // Filtered by level
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Get current time
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;

    struct tm tm_buf;
    localtime_r(&now_c, &tm_buf);

    // Format: [2025-01-15 10:30:45.123] [INFO] file:line - message
    std::fprintf(output_, "[%04d-%02d-%02d %02d:%02d:%02d.%03lld] [%s] %s:%d - ",
        tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
        tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
        static_cast<long long>(ms.count()),
        levelToString(level),
        file, line);

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
