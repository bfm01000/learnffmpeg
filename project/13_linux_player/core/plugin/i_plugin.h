#pragma once

#include <string>

namespace player {

// Interface for all loadable plugins.
// Plugins must implement these methods to integrate into the player pipeline.
class IPlugin {
public:
    virtual ~IPlugin() = default;

    // Return the unique plugin name (e.g., "ffmpeg_decoder")
    virtual std::string name() const = 0;

    // Return the plugin version string
    virtual std::string version() const = 0;

    // Initialize the plugin. Return true on success.
    virtual bool init() = 0;

    // Uninitialize the plugin. Called before unloading.
    virtual void uninit() = 0;
};

} // namespace player
