#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace player {

/// @brief 性能统计 —— 聚合测量值
class PerfStats {
public:
    /// @brief 插入一次采样值（纳秒）
    void record(int64_t elapsed_ns);

    /// @brief 重置所有统计
    void reset();

    /// @brief 采样次数
    int64_t count() const { return count_.load(); }

    /// @brief 总耗时（纳秒）
    int64_t total() const { return total_.load(); }

    /// @brief 平均耗时（纳秒）
    double avg() const;

    /// @brief 最小耗时（纳秒）
    int64_t min() const { return min_.load(); }

    /// @brief 最大耗时（纳秒）
    int64_t max() const { return max_.load(); }

    /// @brief 获取统计摘要字符串
    std::string summary() const;

private:
    std::atomic<int64_t> count_{0};
    std::atomic<int64_t> total_{0};
    std::atomic<int64_t> min_{INT64_MAX};
    std::atomic<int64_t> max_{0};
    mutable std::mutex mutex_;  // For summary formatting
};

/// @brief 性能探针 —— RAII 作用域计时器
///
/// 构造时启动计时，析构时自动记录耗时到 PerfStats。
/// 支持嵌套和手动 stop()。
class PerfProbe {
public:
    /// @brief 创建探针并关联到指定名称的统计
    /// @param name 探针名称（用作统计 key）
    explicit PerfProbe(const std::string& name);

    /// @brief 析构时自动 stop()
    ~PerfProbe();

    // No copy
    PerfProbe(const PerfProbe&) = delete;
    PerfProbe& operator=(const PerfProbe&) = delete;

    // Move
    PerfProbe(PerfProbe&& other) noexcept;
    PerfProbe& operator=(PerfProbe&& other) noexcept;

    /// @brief 手动停止计时并记录
    void stop();

    /// @brief 获取该探针已用时间（纳秒），仅在 stop() 前有效
    int64_t elapsed() const;

    /// @brief 按名称获取全局统计
    static PerfStats& stats(const std::string& name);

    /// @brief 打印所有统计
    static void printAll();

    /// @brief 重置所有统计
    static void resetAll();

private:
    using Clock = std::chrono::high_resolution_clock;

    std::string name_;
    Clock::time_point start_;
    bool stopped_;
};

} // namespace player
