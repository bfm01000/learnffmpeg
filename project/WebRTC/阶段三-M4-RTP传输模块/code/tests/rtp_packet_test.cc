#include "rtp_packet.h"

#include <gtest/gtest.h>

using my_webrtc::RtpPacket;

TEST(RtpPacketTest, DefaultConstructorFieldsAreZero) {
    RtpPacket packet;
    EXPECT_FALSE(packet.IsMarker());
    EXPECT_EQ(packet.PayloadType(), 0);
    EXPECT_EQ(packet.SequenceNumber(), 0);
    EXPECT_EQ(packet.Timestamp(), 0u);
    EXPECT_EQ(packet.SynchronizationSource(), 0u);
    EXPECT_EQ(packet.PayloadSize(), 0u);
}

TEST(RtpPacketTest, SerializeAndParseRoundTrip) {
    RtpPacket original;
    original.SetMarker(true);
    original.SetPayloadType(96);
    original.SetSequenceNumber(0x1234);
    original.SetTimestamp(0xDEADBEEF);
    original.SetSynchronizationSource(0x11223344);

    const uint8_t payloadBytes[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    original.SetPayload(payloadBytes, sizeof(payloadBytes));

    uint8_t serializedBuffer[64] = {0};
    const size_t serializedSize =
        original.Serialize(serializedBuffer, sizeof(serializedBuffer));
    ASSERT_EQ(serializedSize, 12u + sizeof(payloadBytes));

    RtpPacket parsed;
    ASSERT_TRUE(parsed.Parse(serializedBuffer, serializedSize));
    EXPECT_TRUE(parsed.IsMarker());
    EXPECT_EQ(parsed.PayloadType(), 96);
    EXPECT_EQ(parsed.SequenceNumber(), 0x1234);
    EXPECT_EQ(parsed.Timestamp(), 0xDEADBEEFu);
    EXPECT_EQ(parsed.SynchronizationSource(), 0x11223344u);
    ASSERT_EQ(parsed.PayloadSize(), sizeof(payloadBytes));
    for (size_t scanIndex = 0; scanIndex < sizeof(payloadBytes); ++scanIndex) {
        EXPECT_EQ(parsed.Payload()[scanIndex], payloadBytes[scanIndex])
            << "字节差异在 offset " << scanIndex;
    }
}

TEST(RtpPacketTest, SequenceNumberSerializedAsBigEndian) {
    RtpPacket packet;
    packet.SetSequenceNumber(0x1234);
    uint8_t buffer[12] = {0};
    ASSERT_EQ(packet.Serialize(buffer, sizeof(buffer)), 12u);
    // Sequence number 在字节 2-3，大端
    EXPECT_EQ(buffer[2], 0x12);
    EXPECT_EQ(buffer[3], 0x34);
}

TEST(RtpPacketTest, TimestampSerializedAsBigEndian) {
    RtpPacket packet;
    packet.SetTimestamp(0xAABBCCDD);
    uint8_t buffer[12] = {0};
    packet.Serialize(buffer, sizeof(buffer));
    EXPECT_EQ(buffer[4], 0xAA);
    EXPECT_EQ(buffer[5], 0xBB);
    EXPECT_EQ(buffer[6], 0xCC);
    EXPECT_EQ(buffer[7], 0xDD);
}

TEST(RtpPacketTest, SsrcSerializedAsBigEndian) {
    RtpPacket packet;
    packet.SetSynchronizationSource(0x01020304);
    uint8_t buffer[12] = {0};
    packet.Serialize(buffer, sizeof(buffer));
    EXPECT_EQ(buffer[8], 0x01);
    EXPECT_EQ(buffer[9], 0x02);
    EXPECT_EQ(buffer[10], 0x03);
    EXPECT_EQ(buffer[11], 0x04);
}

TEST(RtpPacketTest, MarkerBitOccupiesHighBitOfSecondByte) {
    RtpPacket packetWithMarker;
    packetWithMarker.SetMarker(true);
    packetWithMarker.SetPayloadType(96);
    uint8_t bufferWithMarker[12] = {0};
    packetWithMarker.Serialize(bufferWithMarker, sizeof(bufferWithMarker));
    EXPECT_EQ(bufferWithMarker[1] & 0x80, 0x80);
    EXPECT_EQ(bufferWithMarker[1] & 0x7F, 96);

    RtpPacket packetNoMarker;
    packetNoMarker.SetPayloadType(96);
    uint8_t bufferNoMarker[12] = {0};
    packetNoMarker.Serialize(bufferNoMarker, sizeof(bufferNoMarker));
    EXPECT_EQ(bufferNoMarker[1] & 0x80, 0x00);
    EXPECT_EQ(bufferNoMarker[1] & 0x7F, 96);
}

TEST(RtpPacketTest, VersionFieldAlwaysTwoInSerializedOutput) {
    RtpPacket packet;
    uint8_t buffer[12] = {0};
    packet.Serialize(buffer, sizeof(buffer));
    EXPECT_EQ((buffer[0] >> 6) & 0x03, 2);
    // 本实现不支持 padding / 扩展 / CSRC，应全为 0
    EXPECT_EQ(buffer[0] & 0x3F, 0x00);
}

TEST(RtpPacketTest, ParseRejectsShortBuffer) {
    RtpPacket packet;
    const uint8_t shortBuffer[5] = {0x80, 0x60, 0x00, 0x01, 0x00};
    EXPECT_FALSE(packet.Parse(shortBuffer, sizeof(shortBuffer)));
}

TEST(RtpPacketTest, ParseRejectsWrongVersion) {
    RtpPacket packet;
    // Byte 0 = 0x40 → version=1（应为 2）
    const uint8_t buffer[12] = {0x40, 0x60, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00};
    EXPECT_FALSE(packet.Parse(buffer, sizeof(buffer)));
}

TEST(RtpPacketTest, ParseRejectsNonZeroCsrcCount) {
    RtpPacket packet;
    // Byte 0 = 0x83 → version=2, CC=3（本实现不支持 CSRC）
    const uint8_t buffer[12] = {0x83, 0x60, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00};
    EXPECT_FALSE(packet.Parse(buffer, sizeof(buffer)));
}

TEST(RtpPacketTest, ParseRejectsExtensionFlag) {
    RtpPacket packet;
    // Byte 0 = 0x90 → version=2, X=1（本实现不支持扩展头）
    const uint8_t buffer[12] = {0x90, 0x60, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00};
    EXPECT_FALSE(packet.Parse(buffer, sizeof(buffer)));
}

TEST(RtpPacketTest, SerializeReturnsZeroWhenBufferTooSmall) {
    RtpPacket packet;
    const uint8_t payloadBytes[] = {0xAA, 0xBB};
    packet.SetPayload(payloadBytes, sizeof(payloadBytes));
    uint8_t tinyBuffer[10];
    EXPECT_EQ(packet.Serialize(tinyBuffer, sizeof(tinyBuffer)), 0u);
}

TEST(RtpPacketTest, PayloadTypeUpperBitIgnored) {
    RtpPacket packet;
    // payload type 是 7 位，传入 0xFF 应被截断为 0x7F
    packet.SetPayloadType(0xFF);
    EXPECT_EQ(packet.PayloadType(), 0x7F);
}
