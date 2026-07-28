#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>

namespace player {

/// @brief Lock-free single-producer single-consumer circular buffer for PCM samples.
class AudioRingBuffer {
public:
    explicit AudioRingBuffer(size_t capacity_samples = 8192);
    ~AudioRingBuffer();

    AudioRingBuffer(const AudioRingBuffer&) = delete;
    AudioRingBuffer& operator=(const AudioRingBuffer&) = delete;
    AudioRingBuffer(AudioRingBuffer&&) = delete;
    AudioRingBuffer& operator=(AudioRingBuffer&&) = delete;

    /// @brief Write PCM samples into the ring buffer.
    /// @param data     Pointer to sample data (planar or interleaved).
    /// @param samples  Number of samples to write.
    /// @return Number of samples actually written.
    size_t write(const uint8_t* data, size_t samples);

    /// @brief Read PCM samples from the ring buffer.
    /// @param data     Destination pointer.
    /// @param samples  Number of samples requested.
    /// @return Number of samples actually read.
    size_t read(uint8_t* data, size_t samples);

    /// @brief Return the number of samples currently available for reading.
    size_t available() const;

    /// @brief Clear all data and reset read/write positions.
    void clear();

private:
    uint8_t* buffer_{nullptr};
    size_t   capacity_{0};

    // Lock-free head/tail indices (SPSC).
    std::atomic<size_t> write_pos_{0};
    std::atomic<size_t> read_pos_{0};
};

} // namespace player
