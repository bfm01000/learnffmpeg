#include "packet_buffer.h"

#include <algorithm>
#include <cstdint>

namespace my_webrtc {

PacketBuffer::PacketBuffer() {
    Reset();
}

void PacketBuffer::Reset() {
    for (auto& slot : slots_) {
        slot.occupied = false;
        slot.packet = IncomingPacket{};
    }
    storedPacketCount_ = 0;
    hasSeenAnyPacket_ = false;
    newestSequenceNumber_ = 0;
    hasNextSequenceToCheck_ = false;
    nextSequenceToCheck_ = 0;
}

bool PacketBuffer::IsNewerSequenceNumber(uint16_t candidateSeq,
                                          uint16_t referenceSeq) {
    // 把两者差当作有符号 16 位距离判断（处理 wraparound）
    const int16_t signedDelta =
        static_cast<int16_t>(static_cast<uint16_t>(candidateSeq - referenceSeq));
    return signedDelta > 0;
}

InsertResult PacketBuffer::InsertPacket(
    IncomingPacket packet,
    std::vector<uint16_t>* missingSequenceNumbers) {
    const size_t targetSlot = SlotIndex(packet.sequenceNumber);
    Slot& slot = slots_[targetSlot];

    if (slot.occupied) {
        if (slot.packet.sequenceNumber == packet.sequenceNumber) {
            // 完全相同的 SeqNum，重复包
            return InsertResult::kDuplicate;
        }
        // 不同 SeqNum 映射到同一位置——说明环形 buffer 回绕。
        // 仅当新包"更新"（按 wraparound 比较）才覆盖旧的。
        if (!IsNewerSequenceNumber(packet.sequenceNumber,
                                    slot.packet.sequenceNumber)) {
            return InsertResult::kTooOld;
        }
        // 覆盖旧包
        slot.packet = std::move(packet);
        // storedPacketCount_ 不变（一进一出）
    } else {
        slot.packet = std::move(packet);
        slot.occupied = true;
        ++storedPacketCount_;
    }

    const uint16_t currentSequenceNumber = slot.packet.sequenceNumber;

    // 丢包检测：相对于已见过的"最新 SeqNum"
    if (missingSequenceNumbers != nullptr) {
        missingSequenceNumbers->clear();
        if (hasSeenAnyPacket_ &&
            IsNewerSequenceNumber(currentSequenceNumber, newestSequenceNumber_)) {
            const uint16_t expectedNext =
                static_cast<uint16_t>(newestSequenceNumber_ + 1);
            // 列出 expectedNext..currentSequenceNumber-1 中缺失的 SeqNum
            for (uint16_t scanSeq = expectedNext;
                 scanSeq != currentSequenceNumber;
                 scanSeq = static_cast<uint16_t>(scanSeq + 1)) {
                if (!slots_[SlotIndex(scanSeq)].occupied ||
                    slots_[SlotIndex(scanSeq)].packet.sequenceNumber != scanSeq) {
                    missingSequenceNumbers->push_back(scanSeq);
                }
            }
        }
    }

    // 更新 newestSequenceNumber_
    if (!hasSeenAnyPacket_) {
        hasSeenAnyPacket_ = true;
        newestSequenceNumber_ = currentSequenceNumber;
        hasNextSequenceToCheck_ = true;
        nextSequenceToCheck_ = currentSequenceNumber;
    } else if (IsNewerSequenceNumber(currentSequenceNumber,
                                      newestSequenceNumber_)) {
        newestSequenceNumber_ = currentSequenceNumber;
    }

    // 如果新包是某帧的首包，且 SeqNum 比当前 nextSequenceToCheck_ 更早，
    // 把扫描起点回退到这里（处理"尾包先到、首包后到"的乱序场景）。
    if (slot.packet.isFrameFirstPacket && hasNextSequenceToCheck_) {
        if (IsNewerSequenceNumber(nextSequenceToCheck_, currentSequenceNumber)) {
            nextSequenceToCheck_ = currentSequenceNumber;
        }
    }

    return InsertResult::kInserted;
}

std::optional<PacketBuffer::FrameRange>
PacketBuffer::TryFindFrameStartingAt(uint16_t startSequence) {
    const size_t startSlot = SlotIndex(startSequence);
    const Slot& startSlotRef = slots_[startSlot];
    if (!startSlotRef.occupied ||
        startSlotRef.packet.sequenceNumber != startSequence) {
        return std::nullopt;
    }
    if (!startSlotRef.packet.isFrameFirstPacket) {
        return std::nullopt;
    }

    const uint32_t frameTimestamp = startSlotRef.packet.rtpTimestamp;
    const bool frameIsKey = startSlotRef.packet.isKeyFrame;
    uint16_t scanSequence = startSequence;

    // 最多扫到环形 buffer 容量，避免无限循环
    for (size_t scanSteps = 0; scanSteps < kBufferCapacity; ++scanSteps) {
        const size_t scanIndex = SlotIndex(scanSequence);
        const Slot& scanSlot = slots_[scanIndex];
        if (!scanSlot.occupied ||
            scanSlot.packet.sequenceNumber != scanSequence) {
            return std::nullopt;  // SeqNum 断层
        }
        // 时间戳必须和首包一致
        if (scanSlot.packet.rtpTimestamp != frameTimestamp) {
            return std::nullopt;
        }
        if (scanSlot.packet.isFrameLastPacket) {
            FrameRange range;
            range.firstSequence = startSequence;
            range.lastSequence = scanSequence;
            range.rtpTimestamp = frameTimestamp;
            range.isKeyFrame = frameIsKey;
            return range;
        }
        scanSequence = static_cast<uint16_t>(scanSequence + 1);
    }
    return std::nullopt;
}

void PacketBuffer::EmitFrameFromRange(const FrameRange& range,
                                      CompletedFrame* outFrame) {
    outFrame->assembledFrameBytes.clear();
    outFrame->rtpTimestamp = range.rtpTimestamp;
    outFrame->isKeyFrame = range.isKeyFrame;
    outFrame->firstSequenceNumber = range.firstSequence;
    outFrame->lastSequenceNumber = range.lastSequence;
    outFrame->renderTimeMs = 0;  // 由 JitterBuffer 上层填

    uint16_t scanSequence = range.firstSequence;
    while (true) {
        const size_t scanIndex = SlotIndex(scanSequence);
        Slot& scanSlot = slots_[scanIndex];
        outFrame->assembledFrameBytes.insert(
            outFrame->assembledFrameBytes.end(),
            scanSlot.packet.naluBytes.begin(),
            scanSlot.packet.naluBytes.end());
        // 释放该 slot
        scanSlot.occupied = false;
        scanSlot.packet = IncomingPacket{};
        --storedPacketCount_;
        if (scanSequence == range.lastSequence) {
            break;
        }
        scanSequence = static_cast<uint16_t>(scanSequence + 1);
    }
}

void PacketBuffer::ExtractCompletedFrames(
    std::vector<CompletedFrame>* outFrames) {
    if (outFrames == nullptr) {
        return;
    }
    if (!hasNextSequenceToCheck_) {
        return;
    }

    // 从 nextSequenceToCheck_ 开始扫，一直扫到 newestSequenceNumber_+1
    // 注意：可能扫多帧。每次成功提取一帧，nextSequenceToCheck_ 推进到下一帧首包。
    while (true) {
        auto rangeOpt = TryFindFrameStartingAt(nextSequenceToCheck_);
        if (!rangeOpt.has_value()) {
            // 当前位置不是有效首包：试着前进到下一个 isFrameFirstPacket 的位置
            // 避免无限卡在"首包丢了但中间到了"的场景
            uint16_t scanSequence =
                static_cast<uint16_t>(nextSequenceToCheck_ + 1);
            bool foundNextStart = false;
            // 最多扫 newestSequenceNumber_ - nextSequenceToCheck_ 步
            for (size_t scanSteps = 0;
                 scanSteps < kBufferCapacity;
                 ++scanSteps) {
                // 不能越过 newestSequenceNumber_
                if (!IsNewerSequenceNumber(
                        static_cast<uint16_t>(newestSequenceNumber_ + 1),
                        scanSequence)) {
                    break;
                }
                const size_t scanIndex = SlotIndex(scanSequence);
                const Slot& scanSlot = slots_[scanIndex];
                if (scanSlot.occupied &&
                    scanSlot.packet.sequenceNumber == scanSequence &&
                    scanSlot.packet.isFrameFirstPacket) {
                    nextSequenceToCheck_ = scanSequence;
                    foundNextStart = true;
                    break;
                }
                scanSequence = static_cast<uint16_t>(scanSequence + 1);
            }
            if (!foundNextStart) {
                break;  // 当前没有更多可解析的首包
            }
            continue;
        }

        // 成功找到一帧
        const FrameRange& range = rangeOpt.value();
        CompletedFrame completedFrame;
        EmitFrameFromRange(range, &completedFrame);
        outFrames->push_back(std::move(completedFrame));

        // 推进到下一帧首包
        nextSequenceToCheck_ =
            static_cast<uint16_t>(range.lastSequence + 1);
    }
}

}  // namespace my_webrtc
