#pragma once

/// @file audio_ring_buffer.h
/// @brief 无锁 SPSC 环形缓冲区。PCM 字节流, Decode Thread 写入, SDL Callback 读取.

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace player {

class AudioRingBuffer {
public:
  /// @param capacityBytes 缓冲区总容量（字节）。应为 2 的幂以优化取模.
  explicit AudioRingBuffer(size_t capacityBytes = 65536);
  ~AudioRingBuffer();

  AudioRingBuffer(const AudioRingBuffer&) = delete;
  AudioRingBuffer& operator=(const AudioRingBuffer&) = delete;
  AudioRingBuffer(AudioRingBuffer&&) = delete;
  AudioRingBuffer& operator=(AudioRingBuffer&&) = delete;

  /// 写入字节. @return 实际写入字节数（可能小于 len 如果空间不足）.
  size_t write(const uint8_t* data, size_t len);

  /// 读取字节. @return 实际读取字节数（可能小于 len 如果数据不足）.
  size_t read(uint8_t* data, size_t len);

  /// 可读字节数
  size_t available() const;

  /// 可写字节数
  size_t writable() const;

  /// 清空
  void clear();

private:
  uint8_t*              m_buffer   = nullptr;
  size_t                m_capacity = 0;
  std::atomic<size_t>   m_writePos{0};
  std::atomic<size_t>   m_readPos{0};
};

} // namespace player
