#pragma once

#include "api/player_types.h"
#include "core/queue/frame_queue.h"
#include "core/queue/packet_queue.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

struct AVFrame;
struct AVPacket;

namespace player {

// Forward declarations
struct Frame;          // from core/memory/frame.h
class ClockManager;    // from core/clock/clock_manager.h

/// @brief Seek 请求处理器
///
/// 负责处理 seek 请求的全流程：
///   暂停时钟 -> 清空队列 -> 刷新解码器 -> demux seek -> 设置时钟 -> 恢复
///
/// "解码到目标帧"不在 SeekHandler 内部完成——它由解码线程自然完成:
///   demux seek 后, 下一个 readPacket() 返回新位置的 packet,
///   解码线程消费它即可得到目标帧.
class SeekHandler {
public:
    using PacketQueueType = PacketQueue<std::shared_ptr<::AVPacket>>;
    using FrameQueueType  = FrameQueue<std::shared_ptr<Frame>>;

    /// @brief 外部依赖集合 —— 由 PlayerController 注入
    struct Dependencies {
        /// Demux seek
        std::function<bool(int64_t position_ms, int flags)> demuxSeekTo;

        /// Flush decoders (increment serial)
        std::function<void()> flushAudioDecoder;
        std::function<void()> flushVideoDecoder;

        /// Clocks (non-owning reference — caller ensures lifetime > SeekHandler)
        ClockManager* clockMgr = nullptr;

        /// Queues
        std::shared_ptr<PacketQueueType>   videoPktQueue;
        std::shared_ptr<PacketQueueType>   audioPktQueue;
        std::shared_ptr<FrameQueueType>    videoFrmQueue;
    };

    explicit SeekHandler(const Dependencies& deps);

    /// @brief 执行 seek
    /// @param position_ms 目标位置（毫秒）
    /// @param flags       SeekFlag 组合
    /// @return true 成功, false 失败（已在 seek 中）
    bool seekTo(int64_t position_ms, int32_t flags = 0);

    /// @brief 当前是否正在 seek
    bool isSeeking() const { return m_seeking.load(std::memory_order_acquire); }

    /// @brief 获取当前 seek 目标位置
    int64_t getTargetPosition() const { return m_targetPosMs.load(std::memory_order_acquire); }

private:
    void flushQueues_();
    void flushDecoders_();

    Dependencies m_deps;
    std::atomic<bool>    m_seeking{false};
    std::atomic<int64_t> m_targetPosMs{0};
};

} // namespace player
