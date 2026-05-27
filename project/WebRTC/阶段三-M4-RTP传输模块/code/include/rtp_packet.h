#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace my_webrtc {

// 简化版 RTP 包（RFC 3550），不支持 CSRC 列表和扩展头。
// 内部用独立字段保存语义，Serialize / Parse 时手动处理网络字节序（大端）。
class RtpPacket {
public:
    static constexpr size_t kFixedHeaderSize = 12;
    static constexpr uint8_t kRtpVersion = 2;

    RtpPacket();

    void SetMarker(bool isMarker);
    void SetPayloadType(uint8_t payloadType);
    void SetSequenceNumber(uint16_t sequenceNumber);
    void SetTimestamp(uint32_t timestamp);
    void SetSynchronizationSource(uint32_t synchronizationSource);
    void SetPayload(const uint8_t* payloadBytes, size_t payloadLength);

    bool IsMarker() const { return markerBit_; }
    uint8_t PayloadType() const { return payloadType_; }
    uint16_t SequenceNumber() const { return sequenceNumber_; }
    uint32_t Timestamp() const { return timestamp_; }
    uint32_t SynchronizationSource() const { return synchronizationSource_; }
    const std::vector<uint8_t>& Payload() const { return payload_; }
    size_t PayloadSize() const { return payload_.size(); }
    size_t TotalSize() const { return kFixedHeaderSize + payload_.size(); }

    // 序列化到 outputBuffer，返回写入字节数；容量不足返回 0。
    size_t Serialize(uint8_t* outputBuffer, size_t outputCapacity) const;

    // 反序列化；成功返回 true，否则保持原状态返回 false。
    bool Parse(const uint8_t* inputBuffer, size_t inputSize);

private:
    bool markerBit_;
    uint8_t payloadType_;
    uint16_t sequenceNumber_;
    uint32_t timestamp_;
    uint32_t synchronizationSource_;
    std::vector<uint8_t> payload_;
};

}  // namespace my_webrtc
