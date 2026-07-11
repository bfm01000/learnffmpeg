//============================================================================
// RtpPacketizer — 发送端：H264 编码帧 → RTP 包序列
//============================================================================
//
// 职责：将一帧 H264 Annex-B 裸流拆分成多个 RTP 包，遵循 RFC 6184 打包规则。
//       采用「一次规划、逐包产出」的迭代器模式——构造时扫描全部 NALU 并规划好
//       每个包的打包方式，调用方通过 NextPacket() 逐个取出。
//
// 打包策略（取决于 NALU 大小和 MTU）：
//   - Single NALU  : NALU ≤ MTU → 一个 NALU 装一个 RTP 包
//   - STAP-A       : 多个极小 NALU（如 SPS+PPS）聚合在一个包里
//   - FU-A         : NALU > MTU → 拆成多个分片 RTP 包
//
// 本实现当前不支持 STAP-A，只做 Single NALU 和 FU-A 两种模式。
//
// 接口：IRtpPacketizer（抽象），方便后续扩展 VP8/VP9/AV1 打包器。
//
// 参考：RFC 6184 §5（H.264 RTP Payload Format）
//============================================================================

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
