#pragma once

/// @file packet_queue.h
/// @brief Bounded MPSC 阻塞队列。Demux → Decoder 线程间唯一数据通道。
///
/// ==========================================================================
/// 为什么需要 PacketQueue（与 FrameQueue 的区别）
/// ==========================================================================
///
///   PacketQueue                          FrameQueue
///   ───────────                          ──────────
///   位置:  Demux → Decoder               位置:  Decoder → Renderer
///   承载:  AVPacket (压缩, ~KB)           承载:  AVFrame  (解码后, 4K ~8MB)
///   容量:  大 (Video 256 / Audio 512)     容量:  极小 (Video 3~5 / Audio 8~16)
///   消费:  pop 即销毁, 读完就没了          消费:  peek/next/prev, 渲染器需反复看同一帧
///   策略:  阻塞 push + 阻塞 pop            策略:  非阻塞 push + 非阻塞 peek
///
/// 为什么 PacketQueue 需要条件变量？
/// ────────────────────────────────
/// PacketQueue 连接的是 Demux（读文件/网络）和 Decoder（CPU 密集），二者速度差可达
/// 100 倍以上。网络流数据到达不可控，必需:
///
///   1. 阻塞 pop — Decoder 读完所有 packet 后, 必须 wait 直到 Demux 产出新数据。
///      用 condvar 实现零 CPU 开销等待, 而不是 spin loop.
///
///   2. 阻塞 push — Demux 读得太快, PacketQueue 必须限制容量（背压）。满了就让
///      Demux 阻塞, 等 Decoder 消费后 condvar 唤醒. 这是防止内存爆炸的关键机制.
///
///   3. 超时机制 — 网络断流时不能无限等, timeout 让 Decoder 可以退出或报告错误.
///
/// 类比: 工厂仓库 — 大批量缓冲, 卡车（Decoder）慢了就让产线（Demux）停下来等.
///       仓库满了产线暂停（阻塞 push）, 仓库空了卡车等待（阻塞 pop）.
///
/// ==========================================================================
/// Flush Token & Serial 机制
/// ==========================================================================
///
/// Seek 时需要清空旧数据但保留队列结构。flush() 做了三件事:
///   1. 清空 m_queue 中所有旧 packet
///   2. 插入一个空 T{} (flush token) — Decoder 读到后知道"该 flush 解码器了"
///   3. 递增 m_serial — Demux 新产出的 packet 会带新 serial, Decoder 可区分新旧
///
/// Thread safety:  内部封装所有同步原语, 外部不可见 mutex/condvar.
///                 单生产者（Demux Thread）+ 单消费者（Decoder Thread）.
///
/// Ownership:      调用者负责 T 的生命周期。队列存储副本/移动.
///
/// Lifecycle:      push → pop 的正常数据流;
///                 flush → 清空 + 插入 flush token + 递增 serial;
///                 abort → 释放所有等待者, 队列进入终止状态.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace player {

template <typename T>
class PacketQueue {
public:
  // ── 生命周期 ──────────────────────────────────────────────────────────

  explicit PacketQueue(size_t capacity = 256)
    : m_capacity(capacity)
    , m_aborted(false)
    , m_serial(0)
  {}

  ~PacketQueue() { abort(); }

  PacketQueue(const PacketQueue&)            = delete;
  PacketQueue& operator=(const PacketQueue&) = delete;
  PacketQueue(PacketQueue&&)                 = delete;
  PacketQueue& operator=(PacketQueue&&)      = delete;

  // ── Producer API (Demux Thread) ────────────────────────────────────────

  /// 阻塞入队, 直到有空位或 abort. timeoutMs < 0 = 无限阻塞.
  /// @return true 入队成功, false 超时或已 abort.
  bool push(T item, int timeoutMs = -1) {
    std::unique_lock<std::mutex> lock(m_mutex);

    if (timeoutMs < 0) {
      m_notFull.wait(lock, [this] {
        return m_queue.size() < m_capacity || m_aborted;
      });
    } else {
      if (!m_notFull.wait_for(lock,
                              std::chrono::milliseconds(timeoutMs),
                              [this] {
                                return m_queue.size() < m_capacity || m_aborted;
                              })) {
        return false;
      }
    }

    if (m_aborted) return false;

    m_queue.push_back(std::move(item));
    m_notEmpty.notify_one();
    return true;
  }

  /// 清空队列, 插入 flush token (默认 T{}), 递增序列号.
  /// 消费者读到 token 后知晓需要 flush 解码器.
  void flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.clear();
    m_queue.push_back(T{});   // flush token
    m_serial.fetch_add(1, std::memory_order_release);
    m_notEmpty.notify_one();
    m_notFull.notify_all();
  }

  /// 永久终止队列, 唤醒所有等待者.
  void abort() {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_aborted = true;
    }
    m_notEmpty.notify_all();
    m_notFull.notify_all();
  }

  // ── Consumer API (Decoder Thread) ──────────────────────────────────────

  /// 阻塞出队. timeoutMs < 0 = 无限阻塞.
  /// @return std::nullopt 表示超时或 abort 且队列空; T{} 表示 flush token.
  std::optional<T> pop(int timeoutMs = -1) {
    std::unique_lock<std::mutex> lock(m_mutex);

    if (timeoutMs < 0) {
      m_notEmpty.wait(lock, [this] {
        return !m_queue.empty() || m_aborted;
      });
    } else {
      if (!m_notEmpty.wait_for(lock,
                               std::chrono::milliseconds(timeoutMs),
                               [this] {
                                 return !m_queue.empty() || m_aborted;
                               })) {
        return std::nullopt;
      }
    }

    if (m_aborted && m_queue.empty()) return std::nullopt;

    T item = std::move(m_queue.front());
    m_queue.pop_front();
    m_notFull.notify_one();
    return item;
  }

  /// 当前序列号。Decoder 用它检测 stale packet（比当前 serial 低的是旧数据）.
  int serial() const { return m_serial.load(std::memory_order_acquire); }

  // ── 查询 ──────────────────────────────────────────────────────────────

  bool isAborted() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_aborted;
  }

  size_t size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
  }

  size_t capacity() const { return m_capacity; }

  /// 重置（仅在确认无生产者/消费者运行时使用）
  void reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.clear();
    m_aborted = false;
    m_serial.store(0, std::memory_order_release);
  }

private:
  size_t                      m_capacity;
  std::deque<T>               m_queue;
  mutable std::mutex          m_mutex;
  std::condition_variable     m_notEmpty;
  std::condition_variable     m_notFull;
  bool                        m_aborted;
  std::atomic<int>            m_serial;
};

} // namespace player
