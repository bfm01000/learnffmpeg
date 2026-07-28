#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace player {

enum class EventType {
    // Playback events
    Play,
    Pause,
    Stop,
    Seek,
    EOS,
    Buffering,
    BufferingEnd,

    // State events
    StateChanged,
    Error,
    Info,

    // Media events
    MediaLoaded,
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
