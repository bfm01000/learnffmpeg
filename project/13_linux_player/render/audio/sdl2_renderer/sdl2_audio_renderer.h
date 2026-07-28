#pragma once

#include <memory>

#include "render/i_renderer.h"
#include "audio_ring_buffer.h"

#include <SDL2/SDL.h>

extern "C" {
#include <libavutil/frame.h>
}

namespace player {

/// @brief SDL2-based audio renderer implementing IRenderer.
///        Opens an SDL audio device, registers a callback that reads
///        from AudioRingBuffer, performs sample format conversion / mixing,
///        and tracks the audio clock for A/V sync.
class SDL2AudioRenderer : public IRenderer {
public:
    SDL2AudioRenderer();
    ~SDL2AudioRenderer() override;

    SDL2AudioRenderer(const SDL2AudioRenderer&) = delete;
    SDL2AudioRenderer& operator=(const SDL2AudioRenderer&) = delete;
    SDL2AudioRenderer(SDL2AudioRenderer&&) = delete;
    SDL2AudioRenderer& operator=(SDL2AudioRenderer&&) = delete;

    // IRenderer interface
    int init(const RenderConfig& cfg) override;
    int render(AVFrame* frame) override;
    void resize(int w, int h) override;
    void destroy() override;

    /// @brief Pause audio playback.
    void pause();

    /// @brief Resume audio playback.
    void resume();

    /// @brief Get the current audio clock value (in seconds).
    double getAudioClock() const;

private:
    SDL_AudioDeviceID audio_device_id_{0};
    SDL_AudioSpec      audio_spec_;

    std::unique_ptr<AudioRingBuffer> ring_buffer_;

    // Audio clock: tracks the playback timestamp.
    double audio_clock_{0.0};

    // Callback is a static function; we store 'this' to dispatch.
    static void audioCallback(void* userdata, Uint8* stream, int len);

    /// @brief Internal callback handler called from the static wrapper.
    void onAudioCallback(Uint8* stream, int len);
};

} // namespace player
