#include "rtp_packetizer.h"

#include <algorithm>
#include <cassert>

namespace my_webrtc {

namespace {

constexpr uint8_t kFuAType = 28;
constexpr uint8_t kFuHeaderStartBit = 0x80;
constexpr uint8_t kFuHeaderEndBit = 0x40;

// 在 buffer 中从 searchStart 起查找下一个 Annex-B 起始码（0x000001 或 0x00000001）。
// 返回起始码"后面"第一个字节的偏移；找不到返回 bufferSize。
size_t FindNextStartCodePayloadOffset(const uint8_t* buffer,
                                      size_t searchStart,
                                      size_t bufferSize) {
    if (bufferSize < 3) {
        return bufferSize;
    }
    for (size_t scanIndex = searchStart; scanIndex + 2 < bufferSize; ++scanIndex) {
        if (buffer[scanIndex] != 0x00 || buffer[scanIndex + 1] != 0x00) {
            continue;
        }
        if (buffer[scanIndex + 2] == 0x01) {
            return scanIndex + 3;
        }
        if (scanIndex + 3 < bufferSize &&
            buffer[scanIndex + 2] == 0x00 &&
            buffer[scanIndex + 3] == 0x01) {
            return scanIndex + 4;
        }
    }
    return bufferSize;
}

// 给定一个起始码后的位置，反推该起始码本身的起点。
// payloadOffset 指向起始码后第一个字节；返回值指向起始码第一个 0x00。
size_t StartCodeBegin(const uint8_t* buffer, size_t payloadOffset) {
    // 如果起始码是 4 字节（0x00 0x00 0x00 0x01），payloadOffset - 4 处是 0x00
    if (payloadOffset >= 4 && buffer[payloadOffset - 4] == 0x00) {
        return payloadOffset - 4;
    }
    return payloadOffset - 3;
}

}  // namespace

H264Packetizer::H264Packetizer(const uint8_t* encodedFrameBytes,
                               size_t encodedFrameSize,
                               PacketSizeLimits packetLimits,
                               H264PacketizationMode packetizationMode)
    : packetLimits_(packetLimits),
      packetizationMode_(packetizationMode),
      currentPacketIndex_(0) {
    ScanNalUnits(encodedFrameBytes, encodedFrameSize);
    PlanAllPackets();
}

void H264Packetizer::ScanNalUnits(const uint8_t* frameBytes, size_t frameSize) {
    if (frameBytes == nullptr || frameSize == 0) {
        return;
    }

    size_t currentNaluStart = FindNextStartCodePayloadOffset(frameBytes, 0, frameSize);
    while (currentNaluStart < frameSize) {
        const size_t nextStartPayloadOffset =
            FindNextStartCodePayloadOffset(frameBytes, currentNaluStart, frameSize);
        size_t naluEndExclusive = frameSize;
        if (nextStartPayloadOffset < frameSize) {
            naluEndExclusive = StartCodeBegin(frameBytes, nextStartPayloadOffset);
        }
        if (naluEndExclusive > currentNaluStart) {
            NaluView view;
            view.data = frameBytes + currentNaluStart;
            view.length = naluEndExclusive - currentNaluStart;
            nalus_.push_back(view);
        }
        currentNaluStart = nextStartPayloadOffset;
    }
}

void H264Packetizer::PlanAllPackets() {
    for (size_t naluIndex = 0; naluIndex < nalus_.size(); ++naluIndex) {
        const size_t naluLength = nalus_[naluIndex].length;
        if (naluLength == 0) {
            continue;
        }
        if (naluLength <= packetLimits_.maxPayloadBytes) {
            PacketPlan plan;
            plan.packetMode = PacketPlan::Mode::kSingleNalu;
            plan.naluIndex = naluIndex;
            plan.fuaPieceStartOffset = 0;
            plan.fuaPieceEndOffset = 0;
            plan.isFuaFirstPiece = false;
            plan.isFuaLastPiece = false;
            packetPlans_.push_back(plan);
        } else {
            // 大 NALU 必须用 FU-A 分片
            assert(packetizationMode_ == H264PacketizationMode::kNonInterleaved &&
                   "Single NALU 模式下 NALU 不能超过 MTU");
            PlanFuAForNalu(naluIndex);
        }
    }
}

void H264Packetizer::PlanFuAForNalu(size_t naluIndex) {
    const size_t naluLength = nalus_[naluIndex].length;

    // FU-A 包结构: [FU Indicator 1B][FU Header 1B][NALU 数据片段]
    // 原 NALU 头被 FU Header 替代（type 字段保留在 FU Header），
    // 可分片的数据是 (naluLength - 1)。
    constexpr size_t kFuOverheadBytes = 2;
    const size_t maxDataBytesPerPiece = packetLimits_.maxPayloadBytes - kFuOverheadBytes;
    const size_t fragmentableDataBytes = naluLength - 1;

    // 总片数（向上取整）
    const size_t totalPieces =
        (fragmentableDataBytes + maxDataBytesPerPiece - 1) / maxDataBytesPerPiece;

    // 均衡分配：让每片字节数尽量相等，避免最后一片极小。
    const size_t averageBytesPerPiece =
        (fragmentableDataBytes + totalPieces - 1) / totalPieces;

    size_t cursorOffset = 1;  // 跳过原 NALU 头
    for (size_t pieceIndex = 0; pieceIndex < totalPieces; ++pieceIndex) {
        const size_t pieceEndOffset =
            std::min(cursorOffset + averageBytesPerPiece, naluLength);

        PacketPlan plan;
        plan.packetMode = PacketPlan::Mode::kFuA;
        plan.naluIndex = naluIndex;
        plan.fuaPieceStartOffset = cursorOffset;
        plan.fuaPieceEndOffset = pieceEndOffset;
        plan.isFuaFirstPiece = (pieceIndex == 0);
        plan.isFuaLastPiece = (pieceIndex == totalPieces - 1);
        packetPlans_.push_back(plan);

        cursorOffset = pieceEndOffset;
    }
}

bool H264Packetizer::NextPacket(RtpPacket* outPacket) {
    if (outPacket == nullptr || currentPacketIndex_ >= packetPlans_.size()) {
        return false;
    }
    const PacketPlan& plan = packetPlans_[currentPacketIndex_];
    bool filled = false;
    switch (plan.packetMode) {
        case PacketPlan::Mode::kSingleNalu:
            filled = FillSingleNaluPacket(plan, outPacket);
            break;
        case PacketPlan::Mode::kFuA:
            filled = FillFuAPacket(plan, outPacket);
            break;
    }
    if (filled) {
        ++currentPacketIndex_;
    }
    return filled;
}

size_t H264Packetizer::RemainingPackets() const {
    if (currentPacketIndex_ >= packetPlans_.size()) {
        return 0;
    }
    return packetPlans_.size() - currentPacketIndex_;
}

bool H264Packetizer::FillSingleNaluPacket(const PacketPlan& plan,
                                          RtpPacket* outPacket) {
    const NaluView& nalu = nalus_[plan.naluIndex];
    outPacket->SetPayload(nalu.data, nalu.length);
    return true;
}

bool H264Packetizer::FillFuAPacket(const PacketPlan& plan, RtpPacket* outPacket) {
    const NaluView& nalu = nalus_[plan.naluIndex];
    const uint8_t originalNaluHeader = nalu.data[0];

    // FU Indicator = F(1) NRI(2) type(5)
    // F+NRI 从原 NALU 头继承，type 固定为 28（FU-A）
    const uint8_t fuIndicatorByte =
        static_cast<uint8_t>((originalNaluHeader & 0xE0) | kFuAType);

    // FU Header = S(1) E(1) R(1) Type(5)
    // Type 取自原 NALU 头的低 5 位
    uint8_t fuHeaderByte = static_cast<uint8_t>(originalNaluHeader & 0x1F);
    if (plan.isFuaFirstPiece) {
        fuHeaderByte = static_cast<uint8_t>(fuHeaderByte | kFuHeaderStartBit);
    }
    if (plan.isFuaLastPiece) {
        fuHeaderByte = static_cast<uint8_t>(fuHeaderByte | kFuHeaderEndBit);
    }

    const size_t pieceDataBytes = plan.fuaPieceEndOffset - plan.fuaPieceStartOffset;
    std::vector<uint8_t> packetPayload;
    packetPayload.reserve(2 + pieceDataBytes);
    packetPayload.push_back(fuIndicatorByte);
    packetPayload.push_back(fuHeaderByte);
    packetPayload.insert(packetPayload.end(),
                        nalu.data + plan.fuaPieceStartOffset,
                        nalu.data + plan.fuaPieceEndOffset);
    outPacket->SetPayload(packetPayload.data(), packetPayload.size());
    return true;
}

}  // namespace my_webrtc
