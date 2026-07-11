//============================================================================
// RtpPacket — RTP 包的数据载体（RFC 3550 简化实现）
//============================================================================
//
// 职责：封装一个 RTP 包的所有字段（头部 + 载荷），提供「结构体 ↔ 网络字节流」
//       的双向转换。这是整个 RTP 模块最基础的数据结构，Packetizer 用它产出包，
//       Depacketizer 用它解析包。
//
// 支持的字段：
//   - Marker (M), Payload Type (PT), Sequence Number, Timestamp, SSRC
//   - 载荷（raw bytes）
//
// 不支持（有意简化）：
//   - CSRC 列表（本实现固定 CC=0）
//   - RTP 扩展头（本实现固定 X=0）
//   - Padding（本实现固定 P=0）
//
// 字节序：Serialize/Parse 内部手动处理网络字节序（大端），不依赖平台字节序。
//
// 参考：RFC 3550 §5.1（RTP Fixed Header Fields）
//============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace my_webrtc {

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
