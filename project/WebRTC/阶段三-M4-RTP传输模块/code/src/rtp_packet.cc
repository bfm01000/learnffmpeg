#include "rtp_packet.h"

#include <algorithm>

namespace my_webrtc {

namespace {

void WriteUint16BigEndian(uint8_t* destination, uint16_t value) {
    destination[0] = static_cast<uint8_t>((value >> 8) & 0xFF);
    destination[1] = static_cast<uint8_t>(value & 0xFF);
}

void WriteUint32BigEndian(uint8_t* destination, uint32_t value) {
    destination[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    destination[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    destination[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    destination[3] = static_cast<uint8_t>(value & 0xFF);
}

uint16_t ReadUint16BigEndian(const uint8_t* source) {
    return static_cast<uint16_t>((static_cast<uint16_t>(source[0]) << 8) |
                                  static_cast<uint16_t>(source[1]));
}

uint32_t ReadUint32BigEndian(const uint8_t* source) {
    return (static_cast<uint32_t>(source[0]) << 24) |
           (static_cast<uint32_t>(source[1]) << 16) |
           (static_cast<uint32_t>(source[2]) << 8) |
            static_cast<uint32_t>(source[3]);
}

}  // namespace

RtpPacket::RtpPacket()
    : markerBit_(false),
      payloadType_(0),
      sequenceNumber_(0),
      timestamp_(0),
      synchronizationSource_(0) {}

void RtpPacket::SetMarker(bool isMarker) {
    markerBit_ = isMarker;
}

void RtpPacket::SetPayloadType(uint8_t payloadType) {
    payloadType_ = payloadType & 0x7F;
}

void RtpPacket::SetSequenceNumber(uint16_t sequenceNumber) {
    sequenceNumber_ = sequenceNumber;
}

void RtpPacket::SetTimestamp(uint32_t timestamp) {
    timestamp_ = timestamp;
}

void RtpPacket::SetSynchronizationSource(uint32_t synchronizationSource) {
    synchronizationSource_ = synchronizationSource;
}

void RtpPacket::SetPayload(const uint8_t* payloadBytes, size_t payloadLength) {
    if (payloadBytes == nullptr || payloadLength == 0) {
        payload_.clear();
        return;
    }
    payload_.assign(payloadBytes, payloadBytes + payloadLength);
}

size_t RtpPacket::Serialize(uint8_t* outputBuffer, size_t outputCapacity) const {
    const size_t requiredSize = kFixedHeaderSize + payload_.size();
    if (outputBuffer == nullptr || outputCapacity < requiredSize) {
        return 0;
    }

    // Byte 0: V(2) P(1) X(1) CC(4)
    // 本实现固定 P=0, X=0, CC=0，所以只写版本号。
    outputBuffer[0] = static_cast<uint8_t>(kRtpVersion << 6);

    // Byte 1: M(1) PT(7)
    outputBuffer[1] = static_cast<uint8_t>((markerBit_ ? 0x80 : 0x00) |
                                            (payloadType_ & 0x7F));

    // Byte 2-3: Sequence Number (big-endian)
    WriteUint16BigEndian(outputBuffer + 2, sequenceNumber_);

    // Byte 4-7: Timestamp (big-endian)
    WriteUint32BigEndian(outputBuffer + 4, timestamp_);

    // Byte 8-11: SSRC (big-endian)
    WriteUint32BigEndian(outputBuffer + 8, synchronizationSource_);

    if (!payload_.empty()) {
        std::copy(payload_.begin(), payload_.end(),
                  outputBuffer + kFixedHeaderSize);
    }
    return requiredSize;
}

bool RtpPacket::Parse(const uint8_t* inputBuffer, size_t inputSize) {
    if (inputBuffer == nullptr || inputSize < kFixedHeaderSize) {
        return false;
    }

    const uint8_t versionField = (inputBuffer[0] >> 6) & 0x03;
    if (versionField != kRtpVersion) {
        return false;
    }

    // 本实现不支持 CSRC 列表和扩展头——遇到就拒绝。
    const uint8_t csrcCount = inputBuffer[0] & 0x0F;
    if (csrcCount != 0) {
        return false;
    }
    const bool hasExtensionHeader = ((inputBuffer[0] >> 4) & 0x01) != 0;
    if (hasExtensionHeader) {
        return false;
    }

    markerBit_ = ((inputBuffer[1] >> 7) & 0x01) != 0;
    payloadType_ = inputBuffer[1] & 0x7F;
    sequenceNumber_ = ReadUint16BigEndian(inputBuffer + 2);
    timestamp_ = ReadUint32BigEndian(inputBuffer + 4);
    synchronizationSource_ = ReadUint32BigEndian(inputBuffer + 8);

    if (inputSize > kFixedHeaderSize) {
        payload_.assign(inputBuffer + kFixedHeaderSize, inputBuffer + inputSize);
    } else {
        payload_.clear();
    }
    return true;
}

}  // namespace my_webrtc
