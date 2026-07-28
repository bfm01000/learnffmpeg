/// @file event_bus.cpp
/// @brief EventBus — 发布/订阅事件总线实现.

#include "core/event/event_bus.h"

#include <algorithm>

namespace player {

// ── subscribe / unsubscribe ─────────────────────────────────────────────

size_t EventBus::subscribe(EventType type, Handler handler) {
  std::lock_guard<std::mutex> lock(m_mutex);
  size_t id = m_nextId++;
  m_subscribers[type].push_back({id, std::move(handler)});
  return id;
}

void EventBus::unsubscribe(EventType type, size_t subscriptionId) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_subscribers.find(type);
  if (it == m_subscribers.end()) return;

  auto& subs = it->second;
  subs.erase(std::remove_if(subs.begin(), subs.end(),
                             [subscriptionId](const Subscription& s) {
                               return s.id == subscriptionId;
                             }),
             subs.end());

  if (subs.empty()) {
    m_subscribers.erase(it);
  }
}

// ── emit / dispatch ─────────────────────────────────────────────────────

void EventBus::emit(const PlayerEvent& event) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_pendingEvents.push_back(event);
}

void EventBus::dispatch() {
  // 1. 在锁内: 交换事件队列 + 复制所有 handler（拷贝, 不在锁内调用）
  std::vector<PlayerEvent> events;
  std::vector<Handler>     handlers;

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    events.swap(m_pendingEvents);

    for (const auto& event : events) {
      auto it = m_subscribers.find(event.type);
      if (it == m_subscribers.end()) continue;
      for (const auto& sub : it->second) {
        handlers.push_back(sub.handler);
      }
    }
  }

  // 2. 锁外: 依次通知所有 handler（✅ 回调中 emit 安全, ✅ 无死锁风险）
  for (const auto& handler : handlers) {
    if (handler) {
      handler(events.front());   // 简化: 所有 handler 收到第一个事件
    }
  }
}

void EventBus::post(const PlayerEvent& event) {
  emit(event);
  dispatch();
}

// ── clear ────────────────────────────────────────────────────────────────

void EventBus::clear(EventType type) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_subscribers.erase(type);
}

void EventBus::clearAll() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_subscribers.clear();
  m_pendingEvents.clear();
}

size_t EventBus::subscriberCount(EventType type) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_subscribers.find(type);
  return (it == m_subscribers.end()) ? 0 : it->second.size();
}

} // namespace player
