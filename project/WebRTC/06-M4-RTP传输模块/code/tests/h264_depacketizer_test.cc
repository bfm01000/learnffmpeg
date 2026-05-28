#include "rtp_depacketizer.h"
#include "rtp_packet.h"
#include "rtp_packetizer.h"

#include <gtest/gtest.h>
#include <vector>

using my_webrtc::DepacketizedNalu;
using my_webrtc::DepacketizeResult;
using my_webrtc::H264Depacketizer;
using my_webrtc::H264Packetizer;
using my_webrtc::H264PacketizationMode;
using my_webrtc::PacketSizeLimits;
using my_webrtc::RtpPacket;

namespace {

std::vector<uint8_t> MakeAnnexBFrame(
    const std::vector<std::vector<uint8_t>>& nalus) {
    std::vector<uint8_t> frame;
    for (const auto& nalu : nalus) {
        frame.push_back(0x00);
        frame.push_back(0x00);
        frame.push_back(0x00);
        frame.push_back(0x01);
        frame.insert(frame.end(), nalu.begin(), nalu.end());
    }
    return frame;
}

std::vector<uint8_t> MakeNalu(uint8_t naluHeader, size_t totalBytes) {
    std::vector<uint8_t> nalu(totalBytes, 0xAB);
    nalu[0] = naluHeader;
    return nalu;
}

}  // namespace

TEST(H264DepacketizerTest, SingleNaluPacketDecodedCorrectly) {
    const auto originalNalu = MakeNalu(0x41 /* type=1, P 帧 */, 100);
    const auto frame = MakeAnnexBFrame({originalNalu});

    H264Packetizer packetizer(frame.data(), frame.size(), PacketSizeLimits{},
                              H264PacketizationMode::kNonInterleaved);
    H264Depacketizer depacketizer;

    RtpPacket packet;
    ASSERT_TRUE(packetizer.NextPacket(&packet));
    packet.SetTimestamp(12345);
    packet.SetSequenceNumber(100);

    EXPECT_EQ(depacketizer.InsertPacket(packet),
              DepacketizeResult::kCompleteNalu);

    DepacketizedNalu reassembled;
    ASSERT_TRUE(depacketizer.PopCompletedNalu(&reassembled));
    EXPECT_EQ(reassembled.naluBytes.size(), 100u);
    EXPECT_EQ(reassembled.naluBytes[0], 0x41);
    EXPECT_EQ(reassembled.rtpTimestamp, 12345u);
    EXPECT_FALSE(reassembled.isKeyFrame);
}

TEST(H264DepacketizerTest, FuAFullReassemblyMatchesOriginalBytes) {
    const auto originalNalu = MakeNalu(0x65 /* IDR */, 5000);
    const auto frame = MakeAnnexBFrame({originalNalu});

    H264Packetizer packetizer(frame.data(), frame.size(), PacketSizeLimits{},
                              H264PacketizationMode::kNonInterleaved);
    H264Depacketizer depacketizer;

    uint16_t sequenceNumber = 200;
    const uint32_t fixedTimestamp = 99999;
    DepacketizeResult lastResult = DepacketizeResult::kInvalidPacket;
    RtpPacket packet;
    while (packetizer.NextPacket(&packet)) {
        packet.SetTimestamp(fixedTimestamp);
        packet.SetSequenceNumber(sequenceNumber++);
        lastResult = depacketizer.InsertPacket(packet);
    }
    EXPECT_EQ(lastResult, DepacketizeResult::kFuaCompleted);

    DepacketizedNalu reassembled;
    ASSERT_TRUE(depacketizer.PopCompletedNalu(&reassembled));
    ASSERT_EQ(reassembled.naluBytes.size(), originalNalu.size());
    for (size_t scanIndex = 0; scanIndex < originalNalu.size(); ++scanIndex) {
        EXPECT_EQ(reassembled.naluBytes[scanIndex], originalNalu[scanIndex])
            << "字节差异在 offset " << scanIndex;
    }
    EXPECT_TRUE(reassembled.isKeyFrame);
    EXPECT_EQ(reassembled.rtpTimestamp, fixedTimestamp);
}

TEST(H264DepacketizerTest, FuAMiddlePacketLossTriggersSequenceGap) {
    const auto originalNalu = MakeNalu(0x65 /* IDR */, 5000);
    const auto frame = MakeAnnexBFrame({originalNalu});

    H264Packetizer packetizer(frame.data(), frame.size(), PacketSizeLimits{},
                              H264PacketizationMode::kNonInterleaved);
    H264Depacketizer depacketizer;

    uint16_t sequenceNumber = 300;
    const uint32_t fixedTimestamp = 88888;
    std::vector<RtpPacket> allPackets;
    RtpPacket packet;
    while (packetizer.NextPacket(&packet)) {
        packet.SetTimestamp(fixedTimestamp);
        packet.SetSequenceNumber(sequenceNumber++);
        allPackets.push_back(packet);
    }
    ASSERT_GE(allPackets.size(), 3u);

    // 喂首片
    EXPECT_EQ(depacketizer.InsertPacket(allPackets[0]),
              DepacketizeResult::kFuaInProgress);
    // 跳过 allPackets[1]（模拟丢包），直接喂 allPackets[2]
    EXPECT_EQ(depacketizer.InsertPacket(allPackets[2]),
              DepacketizeResult::kSequenceGap);

    // 丢包后状态被重置，后续完整 FU-A 流应能正常重组
    DepacketizedNalu reassembled;
    EXPECT_FALSE(depacketizer.PopCompletedNalu(&reassembled));
}

TEST(H264DepacketizerTest, OrphanMiddlePieceWithoutFirstReturnsSequenceGap) {
    H264Depacketizer depacketizer;
    RtpPacket orphanPacket;
    // FU Indicator: F=0, NRI=3, type=28 → (0x60 | 0x1C) = 0x7C
    // FU Header: S=0, E=0, R=0, type=5 → 0x05
    const uint8_t orphanPayload[] = {0x7C, 0x05, 0xAA, 0xBB, 0xCC};
    orphanPacket.SetPayload(orphanPayload, sizeof(orphanPayload));
    orphanPacket.SetSequenceNumber(500);
    orphanPacket.SetTimestamp(77777);

    EXPECT_EQ(depacketizer.InsertPacket(orphanPacket),
              DepacketizeResult::kSequenceGap);
}

TEST(H264DepacketizerTest, IdrSinglePacketFlaggedAsKeyFrame) {
    H264Depacketizer depacketizer;
    RtpPacket idrPacket;
    const auto idrPayload = MakeNalu(0x65 /* IDR */, 100);
    idrPacket.SetPayload(idrPayload.data(), idrPayload.size());
    idrPacket.SetSequenceNumber(600);
    idrPacket.SetTimestamp(66666);

    EXPECT_EQ(depacketizer.InsertPacket(idrPacket),
              DepacketizeResult::kCompleteNalu);

    DepacketizedNalu reassembled;
    ASSERT_TRUE(depacketizer.PopCompletedNalu(&reassembled));
    EXPECT_TRUE(reassembled.isKeyFrame);
}

TEST(H264DepacketizerTest, EmptyPayloadReturnsInvalid) {
    H264Depacketizer depacketizer;
    RtpPacket emptyPacket;
    EXPECT_EQ(depacketizer.InsertPacket(emptyPacket),
              DepacketizeResult::kInvalidPacket);
}

TEST(H264DepacketizerTest, FuATimestampMismatchReturnsInvalid) {
    // 构造一个 FU-A 首片
    H264Depacketizer depacketizer;
    RtpPacket firstPiece;
    // FU Indicator 0x7C，FU Header S=1, type=5 → 0x85
    const uint8_t firstPayload[] = {0x7C, 0x85, 0xAA, 0xBB};
    firstPiece.SetPayload(firstPayload, sizeof(firstPayload));
    firstPiece.SetSequenceNumber(700);
    firstPiece.SetTimestamp(11111);
    EXPECT_EQ(depacketizer.InsertPacket(firstPiece),
              DepacketizeResult::kFuaInProgress);

    // 构造一个时间戳不一致的"非首片"
    RtpPacket badNextPiece;
    const uint8_t middlePayload[] = {0x7C, 0x05, 0xCC, 0xDD};
    badNextPiece.SetPayload(middlePayload, sizeof(middlePayload));
    badNextPiece.SetSequenceNumber(701);
    badNextPiece.SetTimestamp(22222);  // 故意不同
    EXPECT_EQ(depacketizer.InsertPacket(badNextPiece),
              DepacketizeResult::kInvalidPacket);
}

TEST(H264DepacketizerTest, MultiplePacketsConsumedInOrder) {
    // 两个独立的 Single NALU 包，验证 PopCompletedNalu 按 FIFO 顺序输出
    H264Depacketizer depacketizer;

    RtpPacket firstPacket;
    const auto firstPayload = MakeNalu(0x41 /* P 帧 */, 50);
    firstPacket.SetPayload(firstPayload.data(), firstPayload.size());
    firstPacket.SetTimestamp(100);
    firstPacket.SetSequenceNumber(800);

    RtpPacket secondPacket;
    const auto secondPayload = MakeNalu(0x41 /* P 帧 */, 60);
    secondPacket.SetPayload(secondPayload.data(), secondPayload.size());
    secondPacket.SetTimestamp(200);
    secondPacket.SetSequenceNumber(801);

    EXPECT_EQ(depacketizer.InsertPacket(firstPacket),
              DepacketizeResult::kCompleteNalu);
    EXPECT_EQ(depacketizer.InsertPacket(secondPacket),
              DepacketizeResult::kCompleteNalu);

    DepacketizedNalu firstReassembled;
    ASSERT_TRUE(depacketizer.PopCompletedNalu(&firstReassembled));
    EXPECT_EQ(firstReassembled.rtpTimestamp, 100u);
    EXPECT_EQ(firstReassembled.naluBytes.size(), 50u);

    DepacketizedNalu secondReassembled;
    ASSERT_TRUE(depacketizer.PopCompletedNalu(&secondReassembled));
    EXPECT_EQ(secondReassembled.rtpTimestamp, 200u);
    EXPECT_EQ(secondReassembled.naluBytes.size(), 60u);

    DepacketizedNalu emptyPop;
    EXPECT_FALSE(depacketizer.PopCompletedNalu(&emptyPop));
}
