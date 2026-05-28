#pragma once

#include "rtp_packet.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace my_webrtc {

enum class H264PacketizationMode {
    kSingleNaluOnly,    // 兼容老设备：每个 NALU 必须 <= MTU，禁用 FU-A
    kNonInterleaved     // 标准模式：允许 Single NALU / STAP-A / FU-A
};

struct PacketSizeLimits {
    size_t maxPayloadBytes = 1200;  // 不含 RTP 头的载荷上限
};

// 打包器接口：迭代器模式逐包产出
class IRtpPacketizer {
public:
    virtual ~IRtpPacketizer() = default;

    // 产出下一个 RTP 包（仅填充 payload）。调用方负责填 PT/Seq/Ts/SSRC/Marker。
    // 返回 false 表示已无更多包。
    virtual bool NextPacket(RtpPacket* outPacket) = 0;

    // 剩余包数，供 Pacer 估算发送时长。
    virtual size_t RemainingPackets() const = 0;
};

// H264 打包器：输入 Annex-B 格式的编码帧字节流（一帧可能含多个 NALU）。
// 构造时一次性扫描 + 规划全部包，NextPacket 按计划吐包。
class H264Packetizer : public IRtpPacketizer {
public:
    H264Packetizer(const uint8_t* encodedFrameBytes,
                   size_t encodedFrameSize,
                   PacketSizeLimits packetLimits,
                   H264PacketizationMode packetizationMode);

    bool NextPacket(RtpPacket* outPacket) override;
    size_t RemainingPackets() const override;

private:
    struct PacketPlan {
        enum class Mode { kSingleNalu, kFuA };
        Mode packetMode;
        size_t naluIndex;
        size_t fuaPieceStartOffset;
        size_t fuaPieceEndOffset;
        bool isFuaFirstPiece;
        bool isFuaLastPiece;
    };

    struct NaluView {
        const uint8_t* data;
        size_t length;
    };

    void ScanNalUnits(const uint8_t* frameBytes, size_t frameSize);
    void PlanAllPackets();
    void PlanFuAForNalu(size_t naluIndex);

    bool FillSingleNaluPacket(const PacketPlan& plan, RtpPacket* outPacket);
    bool FillFuAPacket(const PacketPlan& plan, RtpPacket* outPacket);

    PacketSizeLimits packetLimits_;
    H264PacketizationMode packetizationMode_;
    std::vector<NaluView> nalus_;
    std::vector<PacketPlan> packetPlans_;
    size_t currentPacketIndex_;
};

}  // namespace my_webrtc
