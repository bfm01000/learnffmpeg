#include "perf_probe.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace player {

// ══════════════════════════════════════════════════════════════════════════════
// PerfStats
// ══════════════════════════════════════════════════════════════════════════════

void PerfStats::record(int64_t elapsed_ns)
{
    count_.fetch_add(1);
    total_.fetch_add(elapsed_ns);

    // Update min
    int64_t current_min = min_.load();
    while (elapsed_ns < current_min) {
        if (min_.compare_exchange_weak(current_min, elapsed_ns)) {
            break;
        }
    }

    // Update max
    int64_t current_max = max_.load();
    while (elapsed_ns > current_max) {
        if (max_.compare_exchange_weak(current_max, elapsed_ns)) {
            break;
        }
    }
}

void PerfStats::reset()
{
    count_.store(0);
    total_.store(0);
    min_.store(INT64_MAX);
    max_.store(0);
}

double PerfStats::avg() const
{
    int64_t c = count_.load();
    int64_t t = total_.load();
    return (c > 0) ? static_cast<double>(t) / static_cast<double>(c) : 0.0;
}

std::string PerfStats::summary() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;

    int64_t c = count_.load();
    if (c == 0) {
        oss << "count=0";
        return oss.str();
    }

    double avg_ns = avg();
    oss << "count=" << c
        << " total=" << total_.load() << "ns"
        << " avg=" << avg_ns << "ns"
        << " min=" << min_.load() << "ns"
        << " max=" << max_.load() << "ns";

    return oss.str();
}

// ══════════════════════════════════════════════════════════════════════════════
// PerfProbe
// ══════════════════════════════════════════════════════════════════════════════

namespace {
    std::map<std::string, PerfStats>& globalStats()
    {
        static std::map<std::string, PerfStats> stats;
        return stats;
    }

    std::mutex& globalStatsMutex()
    {
        static std::mutex mtx;
        return mtx;
    }
}

PerfProbe::PerfProbe(const std::string& name)
    : name_(name)
    , start_(Clock::now())
    , stopped_(false)
{
}

PerfProbe::~PerfProbe()
{
    if (!stopped_) {
        stop();
    }
}

PerfProbe::PerfProbe(PerfProbe&& other) noexcept
    : name_(std::move(other.name_))
    , start_(other.start_)
    , stopped_(other.stopped_)
{
    other.stopped_ = true; // Prevent destructor from recording again
}

PerfProbe& PerfProbe::operator=(PerfProbe&& other) noexcept
{
    if (this != &other) {
        if (!stopped_) {
            stop();
        }
        name_ = std::move(other.name_);
        start_ = other.start_;
        stopped_ = other.stopped_;
        other.stopped_ = true;
    }
    return *this;
}

void PerfProbe::stop()
{
    if (stopped_) {
        return;
    }
    stopped_ = true;

    auto end = Clock::now();
    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count();

    std::lock_guard<std::mutex> lock(globalStatsMutex());
    globalStats()[name_].record(elapsed_ns);
}

int64_t PerfProbe::elapsed() const
{
    if (stopped_) {
        return 0;
    }
    auto now = Clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now - start_).count();
}

PerfStats& PerfProbe::stats(const std::string& name)
{
    std::lock_guard<std::mutex> lock(globalStatsMutex());
    return globalStats()[name];
}

void PerfProbe::printAll()
{
    std::lock_guard<std::mutex> lock(globalStatsMutex());

    std::fprintf(stdout, "=== PerfProbe Statistics ===\n");
    for (const auto& [name, stats] : globalStats()) {
        std::fprintf(stdout, "  %-30s | %s\n", name.c_str(), stats.summary().c_str());
    }
    std::fprintf(stdout, "============================\n");
}

void PerfProbe::resetAll()
{
    std::lock_guard<std::mutex> lock(globalStatsMutex());
    for (auto& [_, stats] : globalStats()) {
        stats.reset();
    }
}

} // namespace player
