#pragma once

#include "api/player_types.h"
#include "core/event/event_types.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace player {

/// @brief 有限状态机 —— 管理播放器生命周期状态流转
class StateMachine {
public:
    /// @brief 状态转换规则
    struct Transition {
        EventType       event;    ///< 触发事件
        PlayerState     from;     ///< 源状态
        PlayerState     to;       ///< 目标状态
        std::function<void()> action; ///< 转换时执行的动作
    };

    /// @brief 状态变更通知
    using StateListener = std::function<void(PlayerState old_state, PlayerState new_state)>;

    StateMachine();

    /// @brief 尝试状态转换
    /// @param event 触发事件
    /// @return true 转换成功，false 无匹配规则
    bool transit(EventType event);

    /// @brief 检查是否可转换
    bool canTransit(EventType event) const;

    /// @brief 获取当前状态
    PlayerState getState() const;

    /// @brief 注册状态转换规则
    void registerTransition(const Transition& transition);

    /// @brief 注册状态变更监听器
    void addListener(StateListener listener);

    /// @brief 重置到初始状态
    void reset();

private:
    // 从规则表中查找匹配的转换
    const Transition* findTransition(EventType event) const;

    PlayerState state_;
    mutable std::mutex mutex_;
    std::vector<Transition> transitions_;
    std::vector<StateListener> listeners_;
};

} // namespace player
