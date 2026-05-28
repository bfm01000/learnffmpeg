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
