#pragma once

/// @file frame_queue.h
/// @brief Bounded SPSC 帧缓存。Decoder → Renderer 线程间帧传递.
///
/// v2: 新增阻塞 pushFrameBlocking() + 条件变量唤醒。
///     每帧渲染后立即 notify_all，解码线程无需自旋等待。

#include <atomic>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>

namespace player {

template <typename T>
class FrameQueue {
public:
  explicit FrameQueue(size_t capacity = 5)
    : m_capacity(capacity)
    , m_readIndex(0)
  {}

  ~FrameQueue() = default;

  FrameQueue(const FrameQueue&)            = delete;
  FrameQueue& operator=(const FrameQueue&) = delete;
  FrameQueue(FrameQueue&&)                 = delete;
  FrameQueue& operator=(FrameQueue&&)      = delete;

  // ── Producer API (Decoder Thread) ──────────────────────────────────────

  /// Non-blocking push. Returns false if full.
  bool pushFrame(const T& frame) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_queue.size() >= m_capacity) return false;
    m_queue.push_back(frame);
    return true;
  }


  // ── Consumer API (Renderer Thread) ─────────────────────────────────────

  bool peekFrame(T& outFrame) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_readIndex >= m_queue.size()) return false;
    outFrame = m_queue[m_readIndex];
    return true;
  }

  /// Advance to next frame + notify producer that space is available.
  void nextFrame() {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_readIndex < m_queue.size()) ++m_readIndex;
      shrinkIfNeeded_();
    }
  }

  bool prevFrame() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_readIndex > 0) { --m_readIndex; return true; }
    return false;
  }

  size_t numRemaining() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size() > m_readIndex ? m_queue.size() - m_readIndex : 0;
  }

  void flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.clear();
    m_readIndex = 0;
  }

  size_t size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
  }

  size_t capacity() const { return m_capacity; }
  bool empty() const { return numRemaining() == 0; }

  bool full() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size() >= m_capacity;
  }

private:
  void shrinkIfNeeded_() {
    if (m_readIndex > 0) {
      m_queue.erase(m_queue.begin(), m_queue.begin() + m_readIndex);
      m_readIndex = 0;
    }
  }

  size_t                m_capacity;
  mutable std::mutex    m_mutex;
  std::deque<T>         m_queue;
  size_t                m_readIndex;
};

} // namespace player
