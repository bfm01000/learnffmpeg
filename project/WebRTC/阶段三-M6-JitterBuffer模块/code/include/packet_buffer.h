#pragma once

#include "jitter_buffer.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace my_webrtc {

// 按 SeqNum 索引的环形缓冲，固定容量。
// 同一帧的多个包按 rtpTimestamp 标识归属，由 ExtractCompletedFrame 拼接成完整帧。
class PacketBuffer {
public:
    static constexpr size_t kBufferCapacity = 2048;

    PacketBuffer();

    // 插入一个包；返回的 missingSequenceNumbers 用于上层 NACK 反馈。
    InsertResult InsertPacket(IncomingPacket packet,
                              std::vector<uint16_t>* missingSequenceNumbers);

    // 扫描 buffer，把所有"已完整"的帧（首包到尾包 SeqNum 连续 + 有 Marker）
    // 按时间戳顺序提取出来。返回的帧字节是多个 NALU 拼接（之间无分隔符）。
    void ExtractCompletedFrames(std::vector<CompletedFrame>* outFrames);

    void Reset();

    size_t StoredPacketCount() const { return storedPacketCount_; }

private:
    struct Slot {
        bool occupied = false;
        IncomingPacket packet;
    };

    static size_t SlotIndex(uint16_t sequenceNumber) {
        return static_cast<size_t>(sequenceNumber) % kBufferCapacity;
    }

    // SeqNum a 是否"比" b 新（考虑 wraparound）
    static bool IsNewerSequenceNumber(uint16_t candidateSeq, uint16_t referenceSeq);

    // 从 startSeq 开始尝试拼一帧；成功返回该帧字节范围 [first, last]
    // 失败返回 nullopt（首包不在 startSeq 上，或 SeqNum 断层，或没收到 Marker）
    struct FrameRange {
        uint16_t firstSequence;
        uint16_t lastSequence;
        uint32_t rtpTimestamp;
        bool isKeyFrame;
    };
    std::optional<FrameRange> TryFindFrameStartingAt(uint16_t startSequence);

    void EmitFrameFromRange(const FrameRange& range, CompletedFrame* outFrame);

    std::array<Slot, kBufferCapacity> slots_;
    size_t storedPacketCount_;

    // 用于 NACK 检测：见过的最新 SeqNum
    bool hasSeenAnyPacket_;
    uint16_t newestSequenceNumber_;

    // 用于增量扫描完整帧：下一次提取从此 SeqNum 开始查找
    bool hasNextSequenceToCheck_;
    uint16_t nextSequenceToCheck_;
};

}  // namespace my_webrtc
