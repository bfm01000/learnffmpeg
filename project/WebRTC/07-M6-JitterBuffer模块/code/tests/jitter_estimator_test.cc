#include "jitter_estimator.h"

#include <gtest/gtest.h>

using my_webrtc::JitterEstimator;

TEST(JitterEstimatorTest, FreshInstanceHasZeroJitter) {
    JitterEstimator estimator;
    EXPECT_EQ(estimator.GetSmoothedJitterMs(), 0);
    EXPECT_EQ(estimator.GetPeakJitterMs(), 0);
}

TEST(JitterEstimatorTest, StableNetworkConvergesToLowJitter) {
    JitterEstimator estimator;
    // 50 帧，每帧实际间隔 = 期望间隔（33ms），抖动应趋向 0
    for (int frameCount = 0; frameCount < 50; ++frameCount) {
        estimator.OnFrameReceived(33, 33);
    }
    EXPECT_LE(estimator.GetSmoothedJitterMs(), 1);
}

TEST(JitterEstimatorTest, ConsistentJitterRaisesEstimate) {
    JitterEstimator estimator;
    // 持续 30ms 抖动，应收敛到 ~30
    for (int frameCount = 0; frameCount < 100; ++frameCount) {
        estimator.OnFrameReceived(63, 33);  // 实际比期望多 30ms
    }
    EXPECT_GE(estimator.GetSmoothedJitterMs(), 25);
    EXPECT_LE(estimator.GetSmoothedJitterMs(), 35);
}

TEST(JitterEstimatorTest, TargetDelayIsThreeTimesJitterPlusDecodeAndRender) {
    JitterEstimator estimator;
    for (int frameCount = 0; frameCount < 100; ++frameCount) {
        estimator.OnFrameReceived(63, 33);
    }
    const int64_t expectedJitterMs = estimator.GetSmoothedJitterMs();
    const int64_t expectedTargetDelay = 3 * expectedJitterMs +
        JitterEstimator::kDefaultDecodeDelayMs +
        JitterEstimator::kDefaultRenderDelayMs;
    EXPECT_NEAR(estimator.GetTargetDelayMs(), expectedTargetDelay, 1);
}

TEST(JitterEstimatorTest, CustomDecodeAndRenderDelaysAreRespected) {
    JitterEstimator estimator;
    estimator.OnFrameReceived(33, 33);
    const int64_t customDecodeMs = 50;
    const int64_t customRenderMs = 20;
    EXPECT_EQ(estimator.GetTargetDelayMs(customDecodeMs, customRenderMs),
              0 + customDecodeMs + customRenderMs);
}

TEST(JitterEstimatorTest, ResetClearsEstimate) {
    JitterEstimator estimator;
    for (int frameCount = 0; frameCount < 50; ++frameCount) {
        estimator.OnFrameReceived(100, 33);
    }
    EXPECT_GT(estimator.GetSmoothedJitterMs(), 10);
    estimator.Reset();
    EXPECT_EQ(estimator.GetSmoothedJitterMs(), 0);
    EXPECT_EQ(estimator.GetPeakJitterMs(), 0);
}

TEST(JitterEstimatorTest, NegativeIntervalDifferenceTreatedAsAbsoluteValue) {
    JitterEstimator estimator;
    // 实际 23ms，期望 33ms → 偏差 10ms，应贡献 +10 抖动样本
    for (int frameCount = 0; frameCount < 100; ++frameCount) {
        estimator.OnFrameReceived(23, 33);
    }
    EXPECT_GE(estimator.GetSmoothedJitterMs(), 8);
    EXPECT_LE(estimator.GetSmoothedJitterMs(), 12);
}
