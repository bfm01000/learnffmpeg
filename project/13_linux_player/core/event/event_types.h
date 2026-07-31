#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace player {

enum class EventType {
    // Playback events
    Open,         // open(url) called
    Play,
    Pause,
    Stop,
    Seek,
    SeekComplete, // seek finished, new position ready
    EOS,
    Buffering,
    BufferingEnd,

    // State events
    StateChanged,
    Error,
    Info,
    Retry,        // user retry after error
    Stopped,      // all threads exited, resources released

    // Media events
    MediaLoaded,  // pipeline init success, ready to play
    MediaUnloaded,
    VideoFormatChanged,
    AudioFormatChanged,

    // Timing events
    FrameDisplayed,
    AudioSamplePlayed,
    DroppedFrames,

    // Plugin events
    PluginLoaded,
    PluginUnloaded,
    PluginError,

    // User-defined
    Custom
};

struct PlayerEvent {
    EventType type;
    int64_t timestamp_ms;
    std::string message;

    // Optional payload
    std::variant<int64_t, double, std::string> data;

    PlayerEvent()
        : type(EventType::Info)
        , timestamp_ms(0)
        , data(int64_t(0))
    {}

    explicit PlayerEvent(EventType t)
        : type(t)
        , timestamp_ms(0)
        , data(int64_t(0))
    {}

    PlayerEvent(EventType t, const std::string& msg)
        : type(t)
        , timestamp_ms(0)
        , message(msg)
        , data(std::string(msg))
    {}
};

} // namespace player
