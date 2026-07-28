#pragma once

/// @file event_bus.h
/// @brief 事件总线 — 线程安全的发布/订阅, 用于模块间松耦合通知.
///
/// ==========================================================================
/// 使用场景
/// ==========================================================================
///   - PlayerController 状态变更通知各模块
///   - 播放进度通知 App 回调
///   - 错误/警告广播
///   - 解码器/渲染器异步事件
///
/// ==========================================================================
/// 线程模型
/// ==========================================================================
///   emit()     — 任意线程调用, 事件进入待分发队列（加锁, 快速返回）
///   dispatch() — Event Thread 定期调用, 遍历队列逐个通知订阅者.
///                回调在 Event Thread 中执行, 不得阻塞.
///   post()     — emit + dispatch 的快捷方式（同步通知当前线程的订阅者）
///
///   ⚠ 回调中禁止 emit/post → 会导致死锁或重入问题.

#include "core/event/event_types.h"

#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

namespace player {

class EventBus {
public:
  using Handler = std::function<void(const PlayerEvent&)>;

  EventBus() = default;
  ~EventBus() = default;

  /// 订阅事件. @return subscription ID, 用于后续取消订阅.
  size_t subscribe(EventType type, Handler handler);

  /// 取消订阅.
  void unsubscribe(EventType type, size_t subscriptionId);

  /// 投递事件到待分发队列（线程安全, 快速返回）.
  void emit(const PlayerEvent& event);

  /// 分发所有待处理事件→通知订阅者. 应在 Event Thread 中调用.
  void dispatch();

  /// 快捷方法: emit + dispatch.
  void post(const PlayerEvent& event);

  /// 清除某类事件的所有订阅者.
  void clear(EventType type);

  /// 清除所有订阅者和待处理事件.
  void clearAll();

  /// 某类事件的订阅者数量.
  size_t subscriberCount(EventType type) const;

private:
  struct Subscription {
    size_t  id;
    Handler handler;
  };

  mutable std::mutex                          m_mutex;
  std::map<EventType, std::vector<Subscription>> m_subscribers;
  std::vector<PlayerEvent>                    m_pendingEvents;
  size_t                                      m_nextId = 0;
};

} // namespace player
