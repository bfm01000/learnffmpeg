#include "jitter_buffer.h"
#include "jitter_estimator.h"
#include "packet_buffer.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <set>

namespace my_webrtc {

namespace {

constexpr int64_t kVideoRtpClockRateHz = 90000;
constexpr int kConsecutiveLossesToTriggerKeyFrame = 10;
constexpr size_t kMaxRecentSeenSize = 256;

// 把 RTP 时间戳差换算成毫秒（视频默认 90kHz 基）
int64_t RtpTimestampDeltaToMs(uint32_t newerRtp, uint32_t olderRtp) {
    const int32_t signedDelta =
        static_cast<int32_t>(static_cast<uint32_t>(newerRtp - olderRtp));
    return static_cast<int64_t>(signedDelta) * 1000 / kVideoRtpClockRateHz;
}

class JitterBufferImpl : public IJitterBuffer {
public:
    JitterBufferImpl();
    ~JitterBufferImpl() override = default;

    InsertResult InsertPacket(IncomingPacket incomingPacket,
                              int64_t arrivalTimeMs) override;
    bool PopNextCompletedFrame(CompletedFrame* outFrame) override;

    int64_t GetEstimatedJitterMs() const override {
        return jitterEstimator_.GetSmoothedJitterMs();
    }
    int64_t GetTargetDelayMs() const override {
        return jitterEstimator_.GetTargetDelayMs();
    }

    void SetObserver(IJitterBufferObserver* observer) override {
        observer_ = observer;
    }
    void Reset() override;

private:
    void OnFrameAssembled(const CompletedFrame& completedFrame,
                          int64_t arrivalTimeMs);

    PacketBuffer packetBuffer_;
    JitterEstimator jitterEstimator_;
    std::vector<CompletedFrame> pendingFrames_;
    IJitterBufferObserver* observer_;

    bool hasLastFrame_;
    uint32_t lastFrameRtpTimestamp_;
    int64_t lastFrameArrivalMs_;

    int consecutiveLossEvents_;

    // 跨帧持久的"最近见过 SeqNum"集合：用于检测 duplicate 包
    // （PacketBuffer 提取帧后会清空 slot，无法独自识别重复）
    std::set<uint16_t> recentSeenSequenceNumbers_;
};

JitterBufferImpl::JitterBufferImpl()
    : observer_(nullptr),
      hasLastFrame_(false),
      lastFrameRtpTimestamp_(0),
      lastFrameArrivalMs_(0),
      consecutiveLossEvents_(0) {}

void JitterBufferImpl::Reset() {
    packetBuffer_.Reset();
    jitterEstimator_.Reset();
    pendingFrames_.clear();
    hasLastFrame_ = false;
    lastFrameRtpTimestamp_ = 0;
    lastFrameArrivalMs_ = 0;
    consecutiveLossEvents_ = 0;
    recentSeenSequenceNumbers_.clear();
}

InsertResult JitterBufferImpl::InsertPacket(IncomingPacket incomingPacket,
                                              int64_t arrivalTimeMs) {
    // Duplicate 检测（持久跨帧）。PacketBuffer 在帧提取后会清空 slot，
    // 无法独立识别重复，由 JitterBufferImpl 用滑动窗口集合兜底。
    if (recentSeenSequenceNumbers_.count(incomingPacket.sequenceNumber) > 0) {
        return InsertResult::kDuplicate;
    }
    if (recentSeenSequenceNumbers_.size() >= kMaxRecentSeenSize) {
        // 简化：满了直接清空。256 个 SeqNum 已超出典型 Jitter Buffer 窗口，
        // 重复重传若跨越这么多 SeqNum 视为新流。
        recentSeenSequenceNumbers_.clear();
    }
    recentSeenSequenceNumbers_.insert(incomingPacket.sequenceNumber);

    std::vector<uint16_t> missingSequenceNumbers;
    const InsertResult insertResult =
        packetBuffer_.InsertPacket(std::move(incomingPacket),
                                    &missingSequenceNumbers);
    if (insertResult != InsertResult::kInserted) {
        return insertResult;
    }

    // 上报丢包给 observer（供 NACK 用）
    if (!missingSequenceNumbers.empty() && observer_ != nullptr) {
        observer_->OnPacketLossDetected(missingSequenceNumbers);
        ++consecutiveLossEvents_;
        if (consecutiveLossEvents_ >= kConsecutiveLossesToTriggerKeyFrame) {
            observer_->OnKeyFrameRequestNeeded();
            consecutiveLossEvents_ = 0;
        }
    }

    // 尝试拼帧
    std::vector<CompletedFrame> newCompletedFrames;
    packetBuffer_.ExtractCompletedFrames(&newCompletedFrames);

    bool anyFrameCompleted = false;
    for (auto& completedFrame : newCompletedFrames) {
        // 1) 用本帧到达数据先更新抗抖估计
        if (hasLastFrame_) {
            const int64_t expectedIntervalMs = RtpTimestampDeltaToMs(
                completedFrame.rtpTimestamp, lastFrameRtpTimestamp_);
            const int64_t actualIntervalMs = arrivalTimeMs - lastFrameArrivalMs_;
            jitterEstimator_.OnFrameReceived(actualIntervalMs, expectedIntervalMs);
        }
        // 2) 基于刚更新的估计算渲染时刻
        completedFrame.renderTimeMs =
            arrivalTimeMs + jitterEstimator_.GetTargetDelayMs();

        // 3) 更新"上一帧"记录
        hasLastFrame_ = true;
        lastFrameRtpTimestamp_ = completedFrame.rtpTimestamp;
        lastFrameArrivalMs_ = arrivalTimeMs;

        // 4) 关键帧到达视为"网络恢复"，重置丢包计数
        if (completedFrame.isKeyFrame) {
            consecutiveLossEvents_ = 0;
        }

        pendingFrames_.push_back(std::move(completedFrame));
        anyFrameCompleted = true;
    }

    return anyFrameCompleted ? InsertResult::kInsertedAndComplete
                              : InsertResult::kInserted;
}

void JitterBufferImpl::OnFrameAssembled(const CompletedFrame& completedFrame,
                                         int64_t arrivalTimeMs) {
    // 保留接口以便扩展（例如未来加 RTCP SR 同步时调）。
    // 当前实现把抗抖估计逻辑直接放在 InsertPacket 的循环里，
    // 因为需要在同一处先更新估计、再算 renderTimeMs。
    (void)completedFrame;
    (void)arrivalTimeMs;
}

bool JitterBufferImpl::PopNextCompletedFrame(CompletedFrame* outFrame) {
    if (outFrame == nullptr || pendingFrames_.empty()) {
        return false;
    }
    *outFrame = std::move(pendingFrames_.front());
    pendingFrames_.erase(pendingFrames_.begin());
    return true;
}

}  // namespace

std::unique_ptr<IJitterBuffer> CreateJitterBuffer() {
    return std::make_unique<JitterBufferImpl>();
}

}  // namespace my_webrtc
