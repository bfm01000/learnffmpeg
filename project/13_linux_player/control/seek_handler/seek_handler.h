#pragma once

#include "api/player_types.h"
#include "core/queue/frame_queue.h"
#include "core/queue/packet_queue.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

struct AVFrame;

namespace player {

// Forward declarations
struct Frame;          // from core/memory/frame.h
class ClockManager;    // from core/clock/clock_manager.h

/// @brief Seek 请求处理器
///
/// 负责处理 seek 请求的全流程：
///   暂停时钟 -> 清空队列 -> 刷新解码器 -> demux seek -> 解码到目标帧 -> 设置时钟 -> 恢复
class SeekHandler {
public:
    using PacketQueueType = PacketQueue<std::shared_ptr<struct AVPacket>>;
    using FrameQueueType  = FrameQueue<std::shared_ptr<Frame>>;

    /// @brief 外部依赖集合 —— 由 PlayerController 注入
    struct Dependencies {
        /// Demuxer 接口（由 PlayerController 提供）
        struct Demuxer {
            /// Seek 到指定位置
            std::function<bool(int64_t position_ms, int flags)> seekTo;
        };

        std::shared_ptr<Demuxer>           demuxer;
        std::shared_ptr<ClockManager>      clock_mgr;
        std::shared_ptr<PacketQueueType>   video_pkt_queue;
        std::shared_ptr<PacketQueueType>   audio_pkt_queue;
        std::shared_ptr<FrameQueueType>    video_frm_queue;
        std::shared_ptr<FrameQueueType>    audio_frm_queue;
    };

    explicit SeekHandler(const Dependencies& deps);

    /// @brief 执行 seek
    /// @param position_ms 目标位置（毫秒）
    /// @param flags       SeekFlag 组合
    /// @return true 成功，false 失败
    bool seekTo(int64_t position_ms, int32_t flags = 0);

    /// @brief 当前是否正在 seek
    bool isSeeking() const { return seeking_.load(); }

    /// @brief 获取当前 seek 目标位置
    int64_t getTargetPosition() const { return target_pos_ms_.load(); }

private:
    void flushQueues();
    void notifySeekComplete(int64_t position_ms);

    std::shared_ptr<Dependencies> deps_;
    std::atomic<bool>   seeking_;
    std::atomic<int64_t> target_pos_ms_;
};

} // namespace player
