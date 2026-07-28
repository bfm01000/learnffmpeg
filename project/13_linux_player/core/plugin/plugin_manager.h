#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "i_plugin.h"

namespace player {

class PluginManager {
public:
    using PluginPtr = std::unique_ptr<IPlugin>;
    using FactoryFn = std::function<PluginPtr()>;

    PluginManager() = default;
    ~PluginManager();

    // Register a plugin factory under a given name
    void registerPlugin(const std::string& name, FactoryFn factory);

    // Create a plugin instance by name
    PluginPtr createPlugin(const std::string& name);

    // Load a plugin from a shared library (.so) using dlopen
    // Returns the plugin name on success, empty string on failure
    std::string loadPlugin(const std::string& library_path);

    // Unload a previously loaded plugin library
    bool unloadPlugin(const std::string& name);

    // Unload all plugins
    void unloadAll();

    // Check if a plugin is registered
    bool hasPlugin(const std::string& name) const;

    // Get list of registered plugin names
    std::vector<std::string> pluginNames() const;

    // Get number of registered plugins
    size_t pluginCount() const;

private:
    struct PluginLibrary {
        void* handle = nullptr;
        std::string name;
    };

    mutable std::mutex mutex_;
    std::map<std::string, FactoryFn> factories_;
    std::map<std::string, PluginLibrary> loaded_libraries_;
};

} // namespace player
