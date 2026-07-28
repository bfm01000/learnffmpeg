#include "sdl2_audio_renderer.h"

#include <cstring>

namespace player {

SDL2AudioRenderer::SDL2AudioRenderer() = default;

SDL2AudioRenderer::~SDL2AudioRenderer() {
    destroy();
}

int SDL2AudioRenderer::init(const RenderConfig& cfg) {
    // TODO: Initialize SDL audio subsystem.
    //       Set audio_spec_ with desired format (e.g. AUDIO_S16SYS, 2 channels, 44100 Hz).
    //       Set audio_spec_.callback = audioCallback, userdata = this.
    //       Open audio device with SDL_OpenAudioDevice().
    //       Create AudioRingBuffer with adequate capacity.
    //       Call SDL_PauseAudioDevice(0) to start playback.
    (void)cfg;
    return 0;
}

int SDL2AudioRenderer::render(AVFrame* frame) {
    // TODO: Convert frame data to the SDL audio format if necessary.
    //       Write samples into ring_buffer_.
    //       Update audio_clock_ based on frame->pts and sample count.
    return 0;
}

void SDL2AudioRenderer::resize(int, int) {
    // Audio renderer does not support resize — no-op.
}

void SDL2AudioRenderer::destroy() {
    // TODO: Close audio device with SDL_CloseAudioDevice().
    //       Quit SDL audio subsystem.
}

void SDL2AudioRenderer::pause() {
    // TODO: Call SDL_PauseAudioDevice(audio_device_id_, 1).
}

void SDL2AudioRenderer::resume() {
    // TODO: Call SDL_PauseAudioDevice(audio_device_id_, 0).
}

double SDL2AudioRenderer::getAudioClock() const {
    // TODO: Return audio_clock_ (computed from bytes written / sample rate).
    return audio_clock_;
}

void SDL2AudioRenderer::audioCallback(void* userdata, Uint8* stream, int len) {
    auto* self = static_cast<SDL2AudioRenderer*>(userdata);
    if (self) {
        self->onAudioCallback(stream, len);
    }
}

void SDL2AudioRenderer::onAudioCallback(Uint8* stream, int len) {
    // TODO: Read up to 'len' bytes from ring_buffer_ into stream.
    //       If underflow occurs, fill the remainder with silence (0).
    //       Update audio_clock_ with the number of samples consumed.
    std::memset(stream, 0, static_cast<size_t>(len));
}

} // namespace player
