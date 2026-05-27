#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace my_webrtc {

// 上游（M4 RtpDepacketizer）送来的一个包，已经把 RTP 头和 NALU 数据解开。
struct IncomingPacket {
    uint16_t sequenceNumber;
    uint32_t rtpTimestamp;
    bool isFrameLastPacket;     // RTP marker bit
    bool isFrameFirstPacket;    // 来自上游 FU-A 解包的标记
    bool isKeyFrame;
    std::vector<uint8_t> naluBytes;
};

struct CompletedFrame {
    std::vector<uint8_t> assembledFrameBytes;  // 多个 NALU 拼成的帧
    uint32_t rtpTimestamp;
    int64_t renderTimeMs;
    bool isKeyFrame;
    uint16_t firstSequenceNumber;
    uint16_t lastSequenceNumber;
};

enum class InsertResult {
    kInserted,            // 入队，本次未触发帧完成
    kInsertedAndComplete, // 入队后触发了至少一帧完成
    kDuplicate,           // 重复包，已丢弃
    kTooOld,              // SeqNum 太老，已超出环形 buffer 窗口
    kInvalid              // 非法包
};

class IJitterBufferObserver {
public:
    virtual ~IJitterBufferObserver() = default;

    // 检测到 SeqNum 断层（中间缺包）时触发，供 NACK 模块用。
    virtual void OnPacketLossDetected(
        const std::vector<uint16_t>& missingSequenceNumbers) = 0;

    // 连续丢包超过阈值时触发，建议请求 IDR 关键帧。
    virtual void OnKeyFrameRequestNeeded() = 0;
};

class IJitterBuffer {
public:
    virtual ~IJitterBuffer() = default;

    virtual InsertResult InsertPacket(IncomingPacket incomingPacket,
                                      int64_t arrivalTimeMs) = 0;

    // 返回 true 表示成功取出一帧；可重复调用直到返回 false。
    virtual bool PopNextCompletedFrame(CompletedFrame* outFrame) = 0;

    virtual int64_t GetEstimatedJitterMs() const = 0;
    virtual int64_t GetTargetDelayMs() const = 0;

    virtual void SetObserver(IJitterBufferObserver* observer) = 0;
    virtual void Reset() = 0;
};

std::unique_ptr<IJitterBuffer> CreateJitterBuffer();

}  // namespace my_webrtc
