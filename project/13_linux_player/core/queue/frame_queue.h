#pragma once

/// @file frame_queue.h
/// @brief Bounded SPSC 非阻塞帧缓存。Decoder → Renderer 线程间帧传递.
///
/// ==========================================================================
/// 为什么需要 FrameQueue（与 PacketQueue 的区别）
/// ==========================================================================
///
///   PacketQueue                          FrameQueue
///   ───────────                          ──────────
///   位置:  Demux → Decoder               位置:  Decoder → Renderer
///   承载:  AVPacket (压缩, ~KB)           承载:  AVFrame  (解码后, 4K ~8MB)
///   容量:  大 (Video 256 / Audio 512)     容量:  极小 (Video 3~5 / Audio 8~16)
///   消费:  pop 即销毁, 读完就没了          消费:  peek/next/prev, 渲染器需反复看同一帧
///   策略:  阻塞 push + 阻塞 pop            策略:  非阻塞 push + 非阻塞 peek
///   机制:  condvar 驱动                    机制:  纯锁保护, 无 condvar
///
/// 为什么 FrameQueue 不需要条件变量？
/// ───────────────────────────────────
/// FrameQueue 连接的是 Decoder 和 Renderer, 二者以帧率为节奏同步（~16ms/帧）。
/// 队列只存 3~5 帧, 约 50~80ms 的缓冲, 刚好吸收解码抖动, 而非应对速度差。
///
///   1. 非阻塞 push — 队列满时 Decoder 不等待。直接 return false, 因为 Renderer
///      最迟 16ms 后会消费一帧腾出空间。spin/retry 的开销远小于 condvar 的 syscall.
///
///   2. 非阻塞 peek — 队列空时 Renderer 不等待。直接返回上一帧继续显示（peekFrame
///      返回上次成功的帧）。播放器不应该因为解码慢一帧就让屏幕黑掉。
///      对比: PacketQueue 的 Decoder 没有数据就必须等 — 没有"上一次的 packet"可用.
///
///   3. 帧步进支持 — 用户可以逐帧前进/后退（nextFrame/prevFrame）。这里 peekFrame
///      返回当前帧而不是消费它，nextFrame 才真正移动到下一帧。这是播放器 pause 后
///      单步调试的核心机制。
///
/// 类比: 双缓冲/三缓冲 — 显示器前永远有画面, 新帧到了就换, 没到就重复显示。
///       不需要 condvar 因为等待上限就是 16ms (一帧时间), 用锁足矣.
///
/// ==========================================================================
/// 容量为什么是 3~5 帧？
/// ==========================================================================
///
///   - 太小 (1帧): Decoder 和 Renderer 严格交替, 解码抖动直接导致掉帧.
///   - 刚好 (3帧): 1帧正显示 + 1帧待显示 + 1帧 Decoder 正在写入.
///                 约 50ms 缓冲, 吸收解码时间波动.
///   - 太大 (>5帧): 增加延迟。用户点暂停, 还有 5 帧没显示, 体验差.
///                  4K 帧 ~8MB, 5 帧 = 40MB GPU 内存, 不可忽视.
///
/// Thread safety:  单生产者（Decoder Thread）+ 单消费者（Renderer Thread）.
///                 内部 mutex 保护所有操作, 外部不可见.
///
/// Ownership:      调用者持有 T 的所有权。帧存入队列后由队列管理生命周期,
///                 Renderer 通过 shared_ptr 共享读取.

#include <atomic>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>

namespace player {

template <typename T>
class FrameQueue {
public:
  // ── 生命周期 ──────────────────────────────────────────────────────────

  /// @param capacity 最大缓存帧数, 建议: Video 3~5, Audio 8~16
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

  /// 非阻塞入队。满时直接返回 false, Decoder 下次再试.
  /// 不做 condvar wait — Renderer 最迟 16ms 后会消费一帧腾出空间.
  bool pushFrame(const T& frame) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_queue.size() >= m_capacity) {
      return false;
    }
    m_queue.push_back(frame);
    return true;
  }

  // ── Consumer API (Renderer Thread) ─────────────────────────────────────

  /// 查看当前待显示的帧（不消费）。Renderer 反复调用此方法获取当前帧.
  /// 无新帧时返回 false, Renderer 继续显示上一帧. 不做 condvar wait.
  bool peekFrame(T& outFrame) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_readIndex >= m_queue.size()) {
      return false;
    }
    outFrame = m_queue[m_readIndex];
    return true;
  }

  /// 移动到下一帧（消费当前帧）。Renderer 显示完当前帧后调用.
  /// 被消费的帧不会立即删除, 而是等到积累足够多后批量清理（amortized O(1)）.
  void nextFrame() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_readIndex < m_queue.size()) {
      ++m_readIndex;
    }
    // 懒惰清理: 攒够了再一次性删除, 避免每次 O(n) 的 deque erase
    shrinkIfNeeded_();
  }

  /// 回到上一帧（帧步进后退）。Pause 状态下用户逐帧后退时使用.
  bool prevFrame() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_readIndex > 0) {
      --m_readIndex;
      return true;
    }
    return false;
  }

  /// 当前帧之后还有多少帧待显示。用于判断是否该触发缓冲.
  size_t numRemaining() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size() > m_readIndex ? m_queue.size() - m_readIndex : 0;
  }

  // ── 生命周期管理 ──────────────────────────────────────────────────────

  /// 清空所有帧。Seek 时调用 — 旧帧不再需要.
  void flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.clear();
    m_readIndex = 0;
  }

  // ── 查询 ──────────────────────────────────────────────────────────────

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
  /// 当已消费帧积攒超过半容量时, 批量删除以摊销 O(n) 开销.
  void shrinkIfNeeded_() {
    if (m_readIndex > m_capacity / 2) {
      m_queue.erase(m_queue.begin(), m_queue.begin() + m_readIndex);
      m_readIndex = 0;
    }
  }

  size_t                m_capacity;
  mutable std::mutex    m_mutex;
  std::deque<T>         m_queue;
  size_t                m_readIndex;  // 当前待显示帧的下标
};

} // namespace player
