#include "rtp_depacketizer.h"

namespace my_webrtc {

namespace {

constexpr uint8_t kFuAType = 28;
constexpr uint8_t kStapAType = 24;
constexpr uint8_t kIdrNaluType = 5;
constexpr uint8_t kFuHeaderStartBit = 0x80;
constexpr uint8_t kFuHeaderEndBit = 0x40;

bool IsKeyFrameNaluType(uint8_t naluType) {
    return naluType == kIdrNaluType;
}

}  // namespace

H264Depacketizer::H264Depacketizer()
    : fuaInProgress_(false),
      fuaTimestamp_(0),
      fuaExpectedNextSequence_(0),
      fuaOriginalNaluType_(0) {}

DepacketizeResult H264Depacketizer::InsertPacket(const RtpPacket& receivedPacket) {
    const auto& payloadBytes = receivedPacket.Payload();
    if (payloadBytes.empty()) {
        return DepacketizeResult::kInvalidPacket;
    }

    const uint8_t firstPayloadByte = payloadBytes[0];
    const uint8_t naluType = firstPayloadByte & 0x1F;

    // Single NALU 模式：NALU type 在 [1, 23]
    if (naluType >= 1 && naluType <= 23) {
        DepacketizedNalu completedNalu;
        completedNalu.naluBytes.assign(payloadBytes.begin(), payloadBytes.end());
        completedNalu.rtpTimestamp = receivedPacket.Timestamp();
        completedNalu.isKeyFrame = IsKeyFrameNaluType(naluType);
        completedNalus_.push_back(std::move(completedNalu));
        return DepacketizeResult::kCompleteNalu;
    }

    if (naluType == kFuAType) {
        return HandleFuAPacket(receivedPacket);
    }

    // STAP-A 等其他模式：本实现不支持，标记为非法。
    if (naluType == kStapAType) {
        return DepacketizeResult::kInvalidPacket;
    }

    return DepacketizeResult::kInvalidPacket;
}

DepacketizeResult H264Depacketizer::HandleFuAPacket(const RtpPacket& packet) {
    const auto& payloadBytes = packet.Payload();
    if (payloadBytes.size() < 2) {
        return DepacketizeResult::kInvalidPacket;
    }

    const uint8_t fuIndicatorByte = payloadBytes[0];
    const uint8_t fuHeaderByte = payloadBytes[1];
    const bool isFirstPiece = (fuHeaderByte & kFuHeaderStartBit) != 0;
    const bool isLastPiece = (fuHeaderByte & kFuHeaderEndBit) != 0;
    const uint8_t originalNaluType = fuHeaderByte & 0x1F;

    if (isFirstPiece) {
        if (fuaInProgress_) {
            // 上一个 FU-A 还没结束就来新首片——丢弃旧状态
            ResetFuAState();
        }
        fuaInProgress_ = true;
        fuaTimestamp_ = packet.Timestamp();
        fuaExpectedNextSequence_ =
            static_cast<uint16_t>(packet.SequenceNumber() + 1);
        fuaOriginalNaluType_ = originalNaluType;

        // 重建原 NALU 头：高 3 位（F+NRI）来自 FU Indicator，低 5 位（type）来自 FU Header
        const uint8_t restoredNaluHeader =
            static_cast<uint8_t>((fuIndicatorByte & 0xE0) | (fuHeaderByte & 0x1F));
        fuaReassemblyBuffer_.clear();
        fuaReassemblyBuffer_.push_back(restoredNaluHeader);
        fuaReassemblyBuffer_.insert(fuaReassemblyBuffer_.end(),
                                    payloadBytes.begin() + 2,
                                    payloadBytes.end());

        if (isLastPiece) {
            // 极端情况：只有一片的 FU-A（不应该发生，但兜底处理）
            DepacketizedNalu completedNalu;
            completedNalu.naluBytes = std::move(fuaReassemblyBuffer_);
            completedNalu.rtpTimestamp = fuaTimestamp_;
            completedNalu.isKeyFrame = IsKeyFrameNaluType(originalNaluType);
            completedNalus_.push_back(std::move(completedNalu));
            ResetFuAState();
            return DepacketizeResult::kFuaCompleted;
        }
        return DepacketizeResult::kFuaInProgress;
    }

    // 非首片
    if (!fuaInProgress_) {
        // 收到中间/尾片但未启动重组——首片可能丢失
        return DepacketizeResult::kSequenceGap;
    }

    if (packet.SequenceNumber() != fuaExpectedNextSequence_) {
        ResetFuAState();
        return DepacketizeResult::kSequenceGap;
    }

    if (packet.Timestamp() != fuaTimestamp_) {
        ResetFuAState();
        return DepacketizeResult::kInvalidPacket;
    }

    fuaReassemblyBuffer_.insert(fuaReassemblyBuffer_.end(),
                                payloadBytes.begin() + 2,
                                payloadBytes.end());
    fuaExpectedNextSequence_ =
        static_cast<uint16_t>(packet.SequenceNumber() + 1);

    if (isLastPiece) {
        DepacketizedNalu completedNalu;
        completedNalu.naluBytes = std::move(fuaReassemblyBuffer_);
        completedNalu.rtpTimestamp = fuaTimestamp_;
        completedNalu.isKeyFrame = IsKeyFrameNaluType(fuaOriginalNaluType_);
        completedNalus_.push_back(std::move(completedNalu));
        ResetFuAState();
        return DepacketizeResult::kFuaCompleted;
    }
    return DepacketizeResult::kFuaInProgress;
}

bool H264Depacketizer::PopCompletedNalu(DepacketizedNalu* outNalu) {
    if (outNalu == nullptr || completedNalus_.empty()) {
        return false;
    }
    *outNalu = std::move(completedNalus_.front());
    completedNalus_.erase(completedNalus_.begin());
    return true;
}

void H264Depacketizer::ResetFuAState() {
    fuaInProgress_ = false;
    fuaReassemblyBuffer_.clear();
    fuaTimestamp_ = 0;
    fuaExpectedNextSequence_ = 0;
    fuaOriginalNaluType_ = 0;
}

}  // namespace my_webrtc
