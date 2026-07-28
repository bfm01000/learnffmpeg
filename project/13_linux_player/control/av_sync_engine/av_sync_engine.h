#pragma once

#include "api/player_types.h"
#include "core/clock/clock_manager.h"

#include <cstdint>
#include <memory>

namespace player {

// Forward declaration (Frame defined in core/memory/frame.h)
struct Frame;

/// @brief 音视频同步引擎
///
/// 根据主时钟计算音视频帧的展示延迟，决定渲染、等待或丢帧。
class AVSyncEngine {
public:
    /// @brief 同步动作
    enum class SyncAction {
        Render,  ///< 立即渲染
        Sleep,   ///< 等待后渲染
        Drop     ///< 丢弃该帧（落后太多）
    };

    explicit AVSyncEngine(ClockManager& clock_manager);

    // ── 同步接口 ──────────────────────────────────────────────────────────

    /// @brief 视频帧同步决策
    /// @param frame 待渲染的视频帧
    /// @return 建议动作
    SyncAction syncVideo(std::shared_ptr<Frame> frame);

    /// @brief 音频帧同步（调整音频时钟）
    /// @param frame 待播放的音频帧
    void syncAudio(std::shared_ptr<Frame> frame);

    // ── 计算 ──────────────────────────────────────────────────────────────

    /// @brief 计算帧 PTS 与主时钟的差值
    /// @param pts      帧时间戳（秒）
    /// @param master   主时钟当前值（秒）
    /// @return 差值（微秒），正数表示帧即将到来，负数表示帧已落后
    int64_t calcDelay(double pts, double master_clock) const;

    // ── 配置 ──────────────────────────────────────────────────────────────

    /// @brief 设置主时钟源
    void setMaster(MasterClockSource source);

    /// @brief 设置同步参数
    void setSyncParams(int64_t max_delay_us, int64_t drop_threshold_us, int64_t tolerance_us);

    // ── 查询 ──────────────────────────────────────────────────────────────

    MasterClockSource getMasterSource() const { return master_source_; }
    int64_t getMaxDelayUs()    const { return max_delay_us_; }
    int64_t getDropThresholdUs() const { return drop_threshold_us_; }
    int64_t getToleranceUs()  const { return tolerance_us_; }

private:
    ClockManager&     clock_manager_;
    MasterClockSource master_source_;
    int64_t           max_delay_us_;        // 最大等待时间（微秒）
    int64_t           drop_threshold_us_;   // 落后阈值，超过则丢帧
    int64_t           tolerance_us_;        // 同步容差（微秒）
};

} // namespace player
