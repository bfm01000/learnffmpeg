#include "audio_ring_buffer.h"

#include <cstring>
#include <algorithm>

namespace player {

AudioRingBuffer::AudioRingBuffer(size_t capacity_samples) {
    // TODO: Allocate buffer_ of capacity_samples bytes.
    //       Store capacity_.
    (void)capacity_samples;
}

AudioRingBuffer::~AudioRingBuffer() {
    // TODO: Free buffer_ with delete[].
}

size_t AudioRingBuffer::write(const uint8_t* data, size_t samples) {
    // TODO: Calculate available write space from atomic indices.
    //       Copy data into buffer_ with possible wrap-around.
    //       Update write_pos_ atomically.
    return 0;
}

size_t AudioRingBuffer::read(uint8_t* data, size_t samples) {
    // TODO: Calculate available read data from atomic indices.
    //       Copy from buffer_ to data with possible wrap-around.
    //       Update read_pos_ atomically.
    return 0;
}

size_t AudioRingBuffer::available() const {
    // TODO: Return (write_pos_ - read_pos_) modulo capacity_.
    return 0;
}

void AudioRingBuffer::clear() {
    // TODO: Reset write_pos_ and read_pos_ to 0.
}

} // namespace player
