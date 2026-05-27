#include "jitter_buffer.h"

#include <gtest/gtest.h>
#include <memory>
#include <vector>

using my_webrtc::CompletedFrame;
using my_webrtc::CreateJitterBuffer;
using my_webrtc::IJitterBuffer;
using my_webrtc::IJitterBufferObserver;
using my_webrtc::IncomingPacket;
using my_webrtc::InsertResult;

namespace {

IncomingPacket MakePacket(uint16_t sequenceNumber,
                          uint32_t rtpTimestamp,
                          bool isFirstPacket,
                          bool isLastPacket,
                          bool isKeyFrame = false) {
    IncomingPacket packet;
    packet.sequenceNumber = sequenceNumber;
    packet.rtpTimestamp = rtpTimestamp;
    packet.isFrameFirstPacket = isFirstPacket;
    packet.isFrameLastPacket = isLastPacket;
    packet.isKeyFrame = isKeyFrame;
    packet.naluBytes = {0xAB};
    return packet;
}

class RecordingObserver : public IJitterBufferObserver {
public:
    void OnPacketLossDetected(
        const std::vector<uint16_t>& missingSequenceNumbers) override {
        ++lossDetectedCallCount;
        for (uint16_t seq : missingSequenceNumbers) {
            allReportedMissing.push_back(seq);
        }
    }

    void OnKeyFrameRequestNeeded() override {
        ++keyFrameRequestCallCount;
    }

    int lossDetectedCallCount = 0;
    int keyFrameRequestCallCount = 0;
    std::vector<uint16_t> allReportedMissing;
};

}  // namespace

TEST(JitterBufferTest, SingleFrameCompletesAndIsPoppable) {
    auto buffer = CreateJitterBuffer();
    EXPECT_EQ(buffer->InsertPacket(MakePacket(100, 1000, true, true, true), 100),
              InsertResult::kInsertedAndComplete);
    CompletedFrame frame;
    ASSERT_TRUE(buffer->PopNextCompletedFrame(&frame));
    EXPECT_EQ(frame.rtpTimestamp, 1000u);
    EXPECT_TRUE(frame.isKeyFrame);
    EXPECT_FALSE(buffer->PopNextCompletedFrame(&frame));
}

TEST(JitterBufferTest, IncompleteFrameStaysPending) {
    auto buffer = CreateJitterBuffer();
    EXPECT_EQ(buffer->InsertPacket(MakePacket(100, 1000, true, false), 100),
              InsertResult::kInserted);
    // 没有尾包，应不能 Pop
    CompletedFrame frame;
    EXPECT_FALSE(buffer->PopNextCompletedFrame(&frame));
}

TEST(JitterBufferTest, RenderTimeIsSetAfterInsert) {
    auto buffer = CreateJitterBuffer();
    const int64_t arrivalTimeMs = 5000;
    buffer->InsertPacket(MakePacket(100, 1000, true, true), arrivalTimeMs);
    CompletedFrame frame;
    ASSERT_TRUE(buffer->PopNextCompletedFrame(&frame));
    // renderTime = arrivalTime + targetDelay；首帧没有抖动数据，
    // 仅 decode+render delay = 20ms。允许 ±5ms 容差。
    EXPECT_NEAR(frame.renderTimeMs, arrivalTimeMs + 20, 5);
}

TEST(JitterBufferTest, ObserverNotifiedOnSequenceGap) {
    auto buffer = CreateJitterBuffer();
    RecordingObserver observer;
    buffer->SetObserver(&observer);

    buffer->InsertPacket(MakePacket(100, 1000, true, true), 100);
    buffer->InsertPacket(MakePacket(105, 2000, true, true), 200);

    EXPECT_EQ(observer.lossDetectedCallCount, 1);
    ASSERT_EQ(observer.allReportedMissing.size(), 4u);
    EXPECT_EQ(observer.allReportedMissing[0], 101);
    EXPECT_EQ(observer.allReportedMissing[3], 104);
}

TEST(JitterBufferTest, ObserverNotifiedForKeyFrameAfterRepeatedLoss) {
    auto buffer = CreateJitterBuffer();
    RecordingObserver observer;
    buffer->SetObserver(&observer);

    // 制造连续丢包事件——每次跳跃 SeqNum 缺 4 个。
    // 注：第一次插入不算 loss 事件（没有"之前的最新 SeqNum"），
    // 所以循环 12 次会产生 11 次 loss，超过阈值 10。
    uint16_t currentSeq = 100;
    uint32_t currentTs = 1000;
    int64_t currentArrival = 100;
    for (int eventIndex = 0; eventIndex < 12; ++eventIndex) {
        buffer->InsertPacket(
            MakePacket(currentSeq, currentTs, true, true), currentArrival);
        currentSeq = static_cast<uint16_t>(currentSeq + 5);
        currentTs += 3000;
        currentArrival += 33;
    }
    EXPECT_GE(observer.keyFrameRequestCallCount, 1);
}

TEST(JitterBufferTest, KeyFrameArrivalResetsLossCounter) {
    auto buffer = CreateJitterBuffer();
    RecordingObserver observer;
    buffer->SetObserver(&observer);

    // 制造 5 次丢包事件（不足以触发关键帧请求）
    uint16_t currentSeq = 100;
    for (int eventIndex = 0; eventIndex < 5; ++eventIndex) {
        buffer->InsertPacket(
            MakePacket(currentSeq, 1000 + eventIndex * 3000, true, true), 100);
        currentSeq = static_cast<uint16_t>(currentSeq + 5);
    }
    EXPECT_EQ(observer.keyFrameRequestCallCount, 0);

    // 关键帧到达
    buffer->InsertPacket(
        MakePacket(currentSeq, 1000 + 5 * 3000, true, true, true), 100);
    currentSeq = static_cast<uint16_t>(currentSeq + 5);

    // 再制造 5 次丢包——因关键帧已重置计数，不应触发
    for (int eventIndex = 0; eventIndex < 5; ++eventIndex) {
        buffer->InsertPacket(
            MakePacket(currentSeq, 10000 + eventIndex * 3000, true, true), 100);
        currentSeq = static_cast<uint16_t>(currentSeq + 5);
    }
    EXPECT_EQ(observer.keyFrameRequestCallCount, 0);
}

TEST(JitterBufferTest, JitterEstimateUpdatesWithFrameArrivals) {
    auto buffer = CreateJitterBuffer();
    // 模拟稳定网络：每帧间隔精确 33ms，RTP timestamp 跳 3000（33.33ms @ 90kHz）
    int64_t arrivalTimeMs = 1000;
    uint32_t rtpTimestamp = 1000;
    uint16_t sequenceNumber = 100;

    for (int frameIndex = 0; frameIndex < 30; ++frameIndex) {
        buffer->InsertPacket(
            MakePacket(sequenceNumber, rtpTimestamp, true, true),
            arrivalTimeMs);
        sequenceNumber = static_cast<uint16_t>(sequenceNumber + 1);
        rtpTimestamp += 3000;
        arrivalTimeMs += 33;
    }
    // 稳定网络下抖动应非常小
    EXPECT_LE(buffer->GetEstimatedJitterMs(), 2);
}

TEST(JitterBufferTest, ResetClearsAllState) {
    auto buffer = CreateJitterBuffer();
    buffer->InsertPacket(MakePacket(100, 1000, true, true), 100);
    CompletedFrame frame;
    ASSERT_TRUE(buffer->PopNextCompletedFrame(&frame));

    buffer->Reset();
    EXPECT_FALSE(buffer->PopNextCompletedFrame(&frame));
    EXPECT_EQ(buffer->GetEstimatedJitterMs(), 0);
}

TEST(JitterBufferTest, DuplicatePacketReturnsDuplicate) {
    auto buffer = CreateJitterBuffer();
    buffer->InsertPacket(MakePacket(100, 1000, true, true), 100);
    auto secondResult = buffer->InsertPacket(
        MakePacket(100, 1000, true, true), 100);
    EXPECT_EQ(secondResult, InsertResult::kDuplicate);
}
