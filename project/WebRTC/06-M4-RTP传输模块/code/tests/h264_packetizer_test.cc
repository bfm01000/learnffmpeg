#include "rtp_packet.h"
#include "rtp_packetizer.h"

#include <gtest/gtest.h>
#include <vector>

using my_webrtc::H264Packetizer;
using my_webrtc::H264PacketizationMode;
using my_webrtc::PacketSizeLimits;
using my_webrtc::RtpPacket;

namespace {

// 构造一个带 4 字节 Annex-B 起始码的多 NALU 字节流
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

// 构造指定大小的 NALU：首字节 = naluHeader，其余填 0xAB
std::vector<uint8_t> MakeNalu(uint8_t naluHeader, size_t totalBytes) {
    std::vector<uint8_t> nalu(totalBytes, 0xAB);
    nalu[0] = naluHeader;
    return nalu;
}

}  // namespace

TEST(H264PacketizerTest, SingleNaluFitsInOnePacket) {
    // 100 字节的 P 帧 NALU（< MTU）
    const auto frame = MakeAnnexBFrame({MakeNalu(0x41 /* type=1 */, 100)});
    PacketSizeLimits limits;
    limits.maxPayloadBytes = 1200;

    H264Packetizer packetizer(frame.data(), frame.size(), limits,
                              H264PacketizationMode::kNonInterleaved);
    EXPECT_EQ(packetizer.RemainingPackets(), 1u);

    RtpPacket packet;
    ASSERT_TRUE(packetizer.NextPacket(&packet));
    EXPECT_EQ(packet.PayloadSize(), 100u);
    EXPECT_EQ(packet.Payload()[0], 0x41);  // 原 NALU 头保留
    EXPECT_FALSE(packetizer.NextPacket(&packet));
}

TEST(H264PacketizerTest, FuAPieceCountIsCorrect) {
    // 10000 字节 IDR NALU，MTU=1200，FU 头 2 字节 → 每片可装 1198
    // 总片数 = ⌈(10000-1)/1198⌉ = ⌈9999/1198⌉ = 9
    const auto frame = MakeAnnexBFrame({MakeNalu(0x65 /* IDR */, 10000)});
    PacketSizeLimits limits;
    limits.maxPayloadBytes = 1200;

    H264Packetizer packetizer(frame.data(), frame.size(), limits,
                              H264PacketizationMode::kNonInterleaved);
    EXPECT_EQ(packetizer.RemainingPackets(), 9u);
}

TEST(H264PacketizerTest, FuAFirstPieceHasStartBit) {
    const auto frame = MakeAnnexBFrame({MakeNalu(0x65 /* IDR */, 10000)});
    PacketSizeLimits limits;
    limits.maxPayloadBytes = 1200;
    H264Packetizer packetizer(frame.data(), frame.size(), limits,
                              H264PacketizationMode::kNonInterleaved);

    RtpPacket firstPacket;
    ASSERT_TRUE(packetizer.NextPacket(&firstPacket));
    const auto& payload = firstPacket.Payload();
    ASSERT_GE(payload.size(), 2u);

    // FU Indicator = (0x65 & 0xE0) | 28 = 0x60 | 0x1C = 0x7C
    EXPECT_EQ(payload[0], 0x7C);
    // FU Header: S=1, E=0, R=0, type=5 → 0x80 | 0x05 = 0x85
    EXPECT_EQ(payload[1], 0x85);
}

TEST(H264PacketizerTest, FuALastPieceHasEndBit) {
    const auto frame = MakeAnnexBFrame({MakeNalu(0x65 /* IDR */, 10000)});
    PacketSizeLimits limits;
    limits.maxPayloadBytes = 1200;
    H264Packetizer packetizer(frame.data(), frame.size(), limits,
                              H264PacketizationMode::kNonInterleaved);

    RtpPacket packet;
    // 跳过前 8 片
    for (size_t pieceIndex = 0; pieceIndex < 8; ++pieceIndex) {
        ASSERT_TRUE(packetizer.NextPacket(&packet));
    }
    // 第 9 片是尾片
    ASSERT_TRUE(packetizer.NextPacket(&packet));
    EXPECT_EQ(packet.Payload()[1] & 0x40, 0x40);  // E=1
    EXPECT_EQ(packet.Payload()[1] & 0x80, 0x00);  // S=0
    EXPECT_FALSE(packetizer.NextPacket(&packet));
}

TEST(H264PacketizerTest, FuAMiddlePiecesHaveNeitherStartNorEnd) {
    const auto frame = MakeAnnexBFrame({MakeNalu(0x65 /* IDR */, 10000)});
    PacketSizeLimits limits;
    limits.maxPayloadBytes = 1200;
    H264Packetizer packetizer(frame.data(), frame.size(), limits,
                              H264PacketizationMode::kNonInterleaved);

    RtpPacket packet;
    ASSERT_TRUE(packetizer.NextPacket(&packet));  // 第 1 片（首）
    for (size_t pieceIndex = 1; pieceIndex < 8; ++pieceIndex) {
        ASSERT_TRUE(packetizer.NextPacket(&packet));
        EXPECT_EQ(packet.Payload()[1] & 0x80, 0x00) << "中间片不应有 S 位";
        EXPECT_EQ(packet.Payload()[1] & 0x40, 0x00) << "中间片不应有 E 位";
    }
}

TEST(H264PacketizerTest, MultipleNalusInOneFrameProduceMultiplePackets) {
    // SPS (30B) + PPS (8B) + IDR (5000B)
    // 各 NALU < MTU 时 SPS/PPS 走 Single NALU；IDR > MTU 走 FU-A
    // (5000-1)/1198 = ⌈4.173⌉ = 5
    const auto frame = MakeAnnexBFrame({
        MakeNalu(0x67 /* SPS */, 30),
        MakeNalu(0x68 /* PPS */, 8),
        MakeNalu(0x65 /* IDR */, 5000),
    });
    PacketSizeLimits limits;
    limits.maxPayloadBytes = 1200;

    H264Packetizer packetizer(frame.data(), frame.size(), limits,
                              H264PacketizationMode::kNonInterleaved);
    // Single(SPS) + Single(PPS) + FU-A(5) = 7
    EXPECT_EQ(packetizer.RemainingPackets(), 7u);
}

TEST(H264PacketizerTest, FuAReassembledByteCountMatchesOriginal) {
    // 3000 字节 IDR NALU，MTU=1200
    // (3000-1)/1198 = ⌈2.503⌉ = 3 片
    const auto frame = MakeAnnexBFrame({MakeNalu(0x65 /* IDR */, 3000)});
    PacketSizeLimits limits;
    limits.maxPayloadBytes = 1200;
    H264Packetizer packetizer(frame.data(), frame.size(), limits,
                              H264PacketizationMode::kNonInterleaved);
    EXPECT_EQ(packetizer.RemainingPackets(), 3u);

    RtpPacket packet;
    size_t totalDataBytesAcrossAllPieces = 0;
    while (packetizer.NextPacket(&packet)) {
        // 每个 FU-A 包扣掉 2 字节 FU 双头
        ASSERT_GE(packet.PayloadSize(), 2u);
        totalDataBytesAcrossAllPieces += packet.PayloadSize() - 2;
    }
    // 重组数据 = NALU 总字节 - 1（原 NALU 头被 FU Header 替代）
    EXPECT_EQ(totalDataBytesAcrossAllPieces, 2999u);
}

TEST(H264PacketizerTest, FuAAllPiecesUseSameType) {
    // 验证所有 FU-A 分片的 FU Header 低 5 位（type）一致
    const auto frame = MakeAnnexBFrame({MakeNalu(0x65 /* IDR */, 5000)});
    PacketSizeLimits limits;
    limits.maxPayloadBytes = 1200;
    H264Packetizer packetizer(frame.data(), frame.size(), limits,
                              H264PacketizationMode::kNonInterleaved);

    RtpPacket packet;
    while (packetizer.NextPacket(&packet)) {
        const uint8_t fuHeaderType = packet.Payload()[1] & 0x1F;
        EXPECT_EQ(fuHeaderType, 5);  // 原 NALU type=5 (IDR)
    }
}

TEST(H264PacketizerTest, EmptyFrameProducesNoPackets) {
    H264Packetizer packetizer(nullptr, 0, PacketSizeLimits{},
                              H264PacketizationMode::kNonInterleaved);
    EXPECT_EQ(packetizer.RemainingPackets(), 0u);
    RtpPacket packet;
    EXPECT_FALSE(packetizer.NextPacket(&packet));
}
