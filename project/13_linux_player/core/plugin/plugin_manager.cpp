#include "plugin_manager.h"

#include <dlfcn.h>
#include <cstring>
#include <iostream>

namespace player {

PluginManager::~PluginManager() {
    unloadAll();
}

void PluginManager::registerPlugin(const std::string& name, FactoryFn factory) {
    std::lock_guard<std::mutex> lock(mutex_);
    factories_[name] = std::move(factory);
}

PluginManager::PluginPtr PluginManager::createPlugin(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = factories_.find(name);
    if (it == factories_.end()) {
        return nullptr;
    }
    return it->second();
}

std::string PluginManager::loadPlugin(const std::string& library_path) {
    void* handle = dlopen(library_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        std::cerr << "PluginManager: failed to load " << library_path
                  << ": " << dlerror() << std::endl;
        return {};
    }

    // Look for a plugin factory function
    using CreateFn = IPlugin* (*)();
    auto* create_fn = reinterpret_cast<CreateFn>(dlsym(handle, "createPlugin"));
    if (!create_fn) {
        std::cerr << "PluginManager: no createPlugin symbol in " << library_path << std::endl;
        dlclose(handle);
        return {};
    }

    // Create a temporary instance to get the name
    IPlugin* instance = create_fn();
    if (!instance) {
        std::cerr << "PluginManager: createPlugin returned null in " << library_path << std::endl;
        dlclose(handle);
        return {};
    }

    std::string name = instance->name();

    // Register the factory using the loaded library's create function
    {
        std::lock_guard<std::mutex> lock(mutex_);
        factories_[name] = [create_fn]() -> PluginPtr {
            return PluginPtr(create_fn());
        };
        loaded_libraries_[name] = {handle, name};
    }

    delete instance;
    return name;
}

bool PluginManager::unloadPlugin(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto lib_it = loaded_libraries_.find(name);
    if (lib_it == loaded_libraries_.end()) {
        return false;
    }

    // Call uninit on all instances? The library manages its own lifecycle.
    // Remove the factory
    factories_.erase(name);

    // Close the shared library
    if (lib_it->second.handle) {
        dlclose(lib_it->second.handle);
    }
    loaded_libraries_.erase(lib_it);
    return true;
}

void PluginManager::unloadAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    factories_.clear();
    for (auto& [name, lib] : loaded_libraries_) {
        if (lib.handle) {
            dlclose(lib.handle);
        }
    }
    loaded_libraries_.clear();
}

bool PluginManager::hasPlugin(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return factories_.find(name) != factories_.end();
}

std::vector<std::string> PluginManager::pluginNames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(factories_.size());
    for (const auto& [name, _] : factories_) {
        names.push_back(name);
    }
    return names;
}

size_t PluginManager::pluginCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return factories_.size();
}

} // namespace player
