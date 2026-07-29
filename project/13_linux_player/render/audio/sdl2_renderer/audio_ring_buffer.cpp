/// @file audio_ring_buffer.cpp
/// @brief AudioRingBuffer — 无锁 SPSC 环形缓冲区实现.

#include "render/audio/sdl2_renderer/audio_ring_buffer.h"

#include <algorithm>
#include <cstring>

namespace player {

AudioRingBuffer::AudioRingBuffer(size_t capacityBytes)
  : m_capacity(capacityBytes)
{
  m_buffer = new uint8_t[m_capacity]();
}

AudioRingBuffer::~AudioRingBuffer() {
  delete[] m_buffer;
}

size_t AudioRingBuffer::write(const uint8_t* data, size_t len) {
  size_t avail = writable();
  if (avail == 0) return 0;

  size_t toWrite = std::min(len, avail);
  size_t wPos    = m_writePos.load(std::memory_order_relaxed);
  size_t cap     = m_capacity;

  // 分两段拷贝以处理 wrap-around
  size_t firstHalf = std::min(toWrite, cap - (wPos & (cap - 1)));
  memcpy(m_buffer + (wPos & (cap - 1)), data, firstHalf);
  memcpy(m_buffer, data + firstHalf, toWrite - firstHalf);

  m_writePos.store(wPos + toWrite, std::memory_order_release);
  return toWrite;
}

size_t AudioRingBuffer::read(uint8_t* data, size_t len) {
  size_t avail = available();
  if (avail == 0) return 0;

  size_t toRead = std::min(len, avail);
  size_t rPos   = m_readPos.load(std::memory_order_relaxed);
  size_t cap    = m_capacity;

  size_t firstHalf = std::min(toRead, cap - (rPos & (cap - 1)));
  memcpy(data, m_buffer + (rPos & (cap - 1)), firstHalf);
  memcpy(data + firstHalf, m_buffer, toRead - firstHalf);

  m_readPos.store(rPos + toRead, std::memory_order_release);
  return toRead;
}

size_t AudioRingBuffer::available() const {
  size_t wPos = m_writePos.load(std::memory_order_acquire);
  size_t rPos = m_readPos.load(std::memory_order_acquire);
  return wPos - rPos;   // 无符号下溢自动处理 wrap-around
}

size_t AudioRingBuffer::writable() const {
  return m_capacity - available();
}

void AudioRingBuffer::clear() {
  m_writePos.store(0, std::memory_order_release);
  m_readPos.store(0, std::memory_order_release);
}

} // namespace player
