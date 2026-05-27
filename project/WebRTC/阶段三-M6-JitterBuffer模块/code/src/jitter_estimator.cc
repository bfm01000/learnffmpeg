#include "jitter_estimator.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace my_webrtc {

JitterEstimator::JitterEstimator() {
    Reset();
}

void JitterEstimator::Reset() {
    smoothedJitterMs_ = 0.0;
    peakJitterMs_ = 0;
}

void JitterEstimator::OnFrameReceived(int64_t actualArrivalIntervalMs,
                                       int64_t expectedArrivalIntervalMs) {
    // 单次抖动样本 = |实际间隔 - 期望间隔|
    const int64_t jitterSampleMs =
        std::llabs(actualArrivalIntervalMs - expectedArrivalIntervalMs);

    // EWMA 更新：new = (1-α) × old + α × sample
    smoothedJitterMs_ =
        (1.0 - kSmoothingFactor) * smoothedJitterMs_ +
        kSmoothingFactor * static_cast<double>(jitterSampleMs);

    // 维持峰值（慢衰减，避免被一个历史大值永久卡住）
    peakJitterMs_ = std::max(peakJitterMs_, jitterSampleMs);
    peakJitterMs_ = static_cast<int64_t>(
        static_cast<double>(peakJitterMs_) * kPeakDecayPerCall);
}

int64_t JitterEstimator::GetSmoothedJitterMs() const {
    return static_cast<int64_t>(std::round(smoothedJitterMs_));
}

int64_t JitterEstimator::GetTargetDelayMs() const {
    return GetTargetDelayMs(kDefaultDecodeDelayMs, kDefaultRenderDelayMs);
}

int64_t JitterEstimator::GetTargetDelayMs(int64_t decodeDelayMs,
                                          int64_t renderDelayMs) const {
    // 3σ 覆盖 99.7% 抖动
    const int64_t jitterDelayMs =
        static_cast<int64_t>(std::round(3.0 * smoothedJitterMs_));
    return jitterDelayMs + decodeDelayMs + renderDelayMs;
}

}  // namespace my_webrtc
