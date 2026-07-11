//============================================================================
// RtpDepacketizer — 接收端：RTP 包序列 → H264 完整 NALU
//============================================================================
//
// 职责：接收 RTP 包（可能乱序、可能 FU-A 分片），重组出完整的 H264 NALU，
//       供 JitterBuffer / 解码器消费。是 Packetizer 的逆操作。
//
// 处理逻辑：
//   - Single NALU 包：直接产出完整 NALU（kCompleteNalu）
//   - FU-A 首片（S=1）：记录 NALU 类型，开始攒 buffer，等后续分片
//   - FU-A 中间片 ：拼入 buffer，检测 SeqNum 是否连续
//   - FU-A 尾片（E=1）：拼入 buffer，标记重组完成（kFuaCompleted）
//
// 状态管理（这是和 Packetizer 的关键区别——Packetizer 无状态，Depacketizer 有状态）：
//   本类维护一个「当前正在重组的 FU-A 序列」状态（fuaInProgress_ / fuaTimestamp_
//   / fuaReassemblyBuffer_ 等），同时只能重组一个 FU-A 序列。如果收到不属于
//   当前序列的包（Timestamp 变了 或 SeqNum 断层），会 Reset 并报 kSequenceGap。
//
// 接口：IRtpDepacketizer（抽象），方便后续扩展其他编码格式。
//
// 参考：RFC 6184 §5.3（NAL Unit Header）§5.8（Fragmentation Units）
//============================================================================

#pragma once

#include "rtp_packet.h"

#include <cstdint>
#include <vector>

namespace my_webrtc {

enum class DepacketizeResult {
    kCompleteNalu,    // Single NALU 模式：当前包就是一个完整 NALU
    kFuaInProgress,   // FU-A 分片重组中
    kFuaCompleted,    // FU-A 尾片到达，重组完成
    kInvalidPacket,   // 包格式非法（空载荷 / 未知 NALU 类型 / 时间戳不一致）
    kSequenceGap      // FU-A 序列检测到中断
};

struct DepacketizedNalu {
    std::vector<uint8_t> naluBytes;   // 完整 NALU，首字节为 NALU header
    uint32_t rtpTimestamp;
    bool isKeyFrame;
};

class IRtpDepacketizer {
public:
    virtual ~IRtpDepacketizer() = default;

    virtual DepacketizeResult InsertPacket(const RtpPacket& receivedPacket) = 0;

    // 取出一个已重组完成的 NALU。若队列为空返回 false。
    virtual bool PopCompletedNalu(DepacketizedNalu* outNalu) = 0;
};

class H264Depacketizer : public IRtpDepacketizer {
public:
    H264Depacketizer();

    DepacketizeResult InsertPacket(const RtpPacket& receivedPacket) override;
    bool PopCompletedNalu(DepacketizedNalu* outNalu) override;

private:
    DepacketizeResult HandleFuAPacket(const RtpPacket& packet);
    void ResetFuAState();

    bool fuaInProgress_;
    uint32_t fuaTimestamp_;
    uint16_t fuaExpectedNextSequence_;
    uint8_t fuaOriginalNaluType_;
    std::vector<uint8_t> fuaReassemblyBuffer_;

    std::vector<DepacketizedNalu> completedNalus_;
};

}  // namespace my_webrtc
