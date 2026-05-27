#pragma once

#include <cstdint>

namespace my_webrtc {

// EWMA 抗抖估计：每收到一帧调一次 OnFrameReceived 更新；
// GetTargetDelayMs 返回应该缓冲多少毫秒。
class JitterEstimator {
public:
    static constexpr double kSmoothingFactor = 0.05;
    static constexpr double kPeakDecayPerCall = 0.95;
    static constexpr int64_t kDefaultDecodeDelayMs = 10;
    static constexpr int64_t kDefaultRenderDelayMs = 10;

    JitterEstimator();

    // 输入：本帧实际到达间隔 vs 期望间隔（基于 RTP 时间戳差换算成 ms）
    void OnFrameReceived(int64_t actualArrivalIntervalMs,
                         int64_t expectedArrivalIntervalMs);

    int64_t GetSmoothedJitterMs() const;
    int64_t GetPeakJitterMs() const { return peakJitterMs_; }

    // 目标延迟 = 3 × 抖动估计 + 解码延迟 + 渲染延迟
    int64_t GetTargetDelayMs() const;
    int64_t GetTargetDelayMs(int64_t decodeDelayMs, int64_t renderDelayMs) const;

    void Reset();

private:
    double smoothedJitterMs_;
    int64_t peakJitterMs_;
};

}  // namespace my_webrtc
