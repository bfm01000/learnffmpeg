#include "jitter_buffer.h"
#include "packet_buffer.h"

#include <gtest/gtest.h>

using my_webrtc::CompletedFrame;
using my_webrtc::IncomingPacket;
using my_webrtc::InsertResult;
using my_webrtc::PacketBuffer;

namespace {

IncomingPacket MakePacket(uint16_t sequenceNumber,
                          uint32_t rtpTimestamp,
                          bool isFirstPacket,
                          bool isLastPacket,
                          bool isKeyFrame = false,
                          std::vector<uint8_t> naluBytes = {}) {
    IncomingPacket packet;
    packet.sequenceNumber = sequenceNumber;
    packet.rtpTimestamp = rtpTimestamp;
    packet.isFrameFirstPacket = isFirstPacket;
    packet.isFrameLastPacket = isLastPacket;
    packet.isKeyFrame = isKeyFrame;
    packet.naluBytes = std::move(naluBytes);
    return packet;
}

}  // namespace

TEST(PacketBufferTest, InsertSinglePacketFrameReturnsInserted) {
    PacketBuffer buffer;
    std::vector<uint16_t> missing;
    auto result = buffer.InsertPacket(
        MakePacket(100, 1000, true, true, false, {0xAA}), &missing);
    EXPECT_EQ(result, InsertResult::kInserted);
    EXPECT_TRUE(missing.empty());
    EXPECT_EQ(buffer.StoredPacketCount(), 1u);
}

TEST(PacketBufferTest, DuplicatePacketReturnsDuplicate) {
    PacketBuffer buffer;
    std::vector<uint16_t> missing;
    buffer.InsertPacket(MakePacket(100, 1000, true, true), &missing);
    auto secondResult = buffer.InsertPacket(
        MakePacket(100, 1000, true, true), &missing);
    EXPECT_EQ(secondResult, InsertResult::kDuplicate);
}

TEST(PacketBufferTest, SinglePacketFrameExtractedCorrectly) {
    PacketBuffer buffer;
    std::vector<uint16_t> missing;
    buffer.InsertPacket(
        MakePacket(100, 1000, true, true, true, {0xAA, 0xBB, 0xCC}), &missing);
    std::vector<CompletedFrame> frames;
    buffer.ExtractCompletedFrames(&frames);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].rtpTimestamp, 1000u);
    EXPECT_TRUE(frames[0].isKeyFrame);
    ASSERT_EQ(frames[0].assembledFrameBytes.size(), 3u);
    EXPECT_EQ(frames[0].assembledFrameBytes[0], 0xAA);
}

TEST(PacketBufferTest, MultiPacketFrameExtractedCorrectly) {
    PacketBuffer buffer;
    std::vector<uint16_t> missing;
    buffer.InsertPacket(
        MakePacket(100, 1000, true, false, true, {0x01}), &missing);
    buffer.InsertPacket(
        MakePacket(101, 1000, false, false, true, {0x02}), &missing);
    buffer.InsertPacket(
        MakePacket(102, 1000, false, true, true, {0x03}), &missing);

    std::vector<CompletedFrame> frames;
    buffer.ExtractCompletedFrames(&frames);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].firstSequenceNumber, 100);
    EXPECT_EQ(frames[0].lastSequenceNumber, 102);
    ASSERT_EQ(frames[0].assembledFrameBytes.size(), 3u);
    EXPECT_EQ(frames[0].assembledFrameBytes[0], 0x01);
    EXPECT_EQ(frames[0].assembledFrameBytes[1], 0x02);
    EXPECT_EQ(frames[0].assembledFrameBytes[2], 0x03);
}

TEST(PacketBufferTest, IncompleteFrameNotExtracted) {
    PacketBuffer buffer;
    std::vector<uint16_t> missing;
    // 100 首包，102 尾包，但 101 缺失
    buffer.InsertPacket(MakePacket(100, 1000, true, false), &missing);
    buffer.InsertPacket(MakePacket(102, 1000, false, true), &missing);
    std::vector<CompletedFrame> frames;
    buffer.ExtractCompletedFrames(&frames);
    EXPECT_TRUE(frames.empty());
}

TEST(PacketBufferTest, OutOfOrderArrivalEventuallyAssembles) {
    PacketBuffer buffer;
    std::vector<uint16_t> missing;
    // 乱序：尾包先到，首包后到
    buffer.InsertPacket(MakePacket(101, 1000, false, true), &missing);
    buffer.InsertPacket(MakePacket(100, 1000, true, false), &missing);

    std::vector<CompletedFrame> frames;
    buffer.ExtractCompletedFrames(&frames);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].firstSequenceNumber, 100);
    EXPECT_EQ(frames[0].lastSequenceNumber, 101);
}

TEST(PacketBufferTest, MissingSequenceNumbersReportedOnGap) {
    PacketBuffer buffer;
    std::vector<uint16_t> missing;
    // 先 SeqNum=100，再跳到 105，期望报告 101-104 缺失
    buffer.InsertPacket(MakePacket(100, 1000, true, true), &missing);
    buffer.InsertPacket(MakePacket(105, 2000, true, true), &missing);
    ASSERT_EQ(missing.size(), 4u);
    EXPECT_EQ(missing[0], 101);
    EXPECT_EQ(missing[1], 102);
    EXPECT_EQ(missing[2], 103);
    EXPECT_EQ(missing[3], 104);
}

TEST(PacketBufferTest, NoMissingReportedForInOrderPackets) {
    PacketBuffer buffer;
    std::vector<uint16_t> missing;
    buffer.InsertPacket(MakePacket(100, 1000, true, true), &missing);
    buffer.InsertPacket(MakePacket(101, 2000, true, true), &missing);
    EXPECT_TRUE(missing.empty());
}

TEST(PacketBufferTest, TwoConsecutiveFramesExtractedInOrder) {
    PacketBuffer buffer;
    std::vector<uint16_t> missing;
    // 帧 A: 100-101
    buffer.InsertPacket(MakePacket(100, 1000, true, false, true, {0x01}),
                        &missing);
    buffer.InsertPacket(MakePacket(101, 1000, false, true, true, {0x02}),
                        &missing);
    // 帧 B: 102-103
    buffer.InsertPacket(MakePacket(102, 2000, true, false, false, {0x03}),
                        &missing);
    buffer.InsertPacket(MakePacket(103, 2000, false, true, false, {0x04}),
                        &missing);

    std::vector<CompletedFrame> frames;
    buffer.ExtractCompletedFrames(&frames);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[0].rtpTimestamp, 1000u);
    EXPECT_TRUE(frames[0].isKeyFrame);
    EXPECT_EQ(frames[1].rtpTimestamp, 2000u);
    EXPECT_FALSE(frames[1].isKeyFrame);
}

TEST(PacketBufferTest, ResetClearsAllState) {
    PacketBuffer buffer;
    std::vector<uint16_t> missing;
    buffer.InsertPacket(MakePacket(100, 1000, true, true), &missing);
    EXPECT_EQ(buffer.StoredPacketCount(), 1u);
    buffer.Reset();
    EXPECT_EQ(buffer.StoredPacketCount(), 0u);
}

TEST(PacketBufferTest, ExtractRemovesEmittedPackets) {
    PacketBuffer buffer;
    std::vector<uint16_t> missing;
    buffer.InsertPacket(MakePacket(100, 1000, true, true), &missing);
    EXPECT_EQ(buffer.StoredPacketCount(), 1u);

    std::vector<CompletedFrame> frames;
    buffer.ExtractCompletedFrames(&frames);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(buffer.StoredPacketCount(), 0u);
}
