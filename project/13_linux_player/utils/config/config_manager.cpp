#include "config_manager.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace player {

ConfigManager::ConfigManager()
    : base_config_(PlayerConfig::localFilePreset())
{
}

bool ConfigManager::loadFromFile(const std::string& path)
{
    // TODO: Implement JSON parsing for config file
    // Expected format:
    // {
    //   "source": { "timeout_ms": 5000, "reconnect": true },
    //   "decode": { "hw_accel": "vaapi" },
    //   "render": { "video": { "vsync": true } }
    // }
    // Currently a no-op stub.

    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    // TODO: Parse JSON and apply to base_config_
    // Consider using nlohmann/json or a lightweight parser.

    file.close();
    return true;
}

void ConfigManager::loadFromEnv()
{
    // Environment variable pattern: PLAYER_SECTION_KEY
    // Example: PLAYER_RENDER_VIDEO_VSYNC=1

    // TODO: Iterate over environment variables.
    // For each matching PLAYER_ prefix, parse the key and value.

    // Placeholder for manual environment loading
    const char* val = nullptr;

    val = std::getenv("PLAYER_SOURCE_TIMEOUT_MS");
    if (val) {
        overrides_["source.timeout_ms"] = std::stoi(val);
    }

    val = std::getenv("PLAYER_SOURCE_RECONNECT");
    if (val) {
        overrides_["source.reconnect"] = (std::string(val) == "1" || std::string(val) == "true");
    }

    val = std::getenv("PLAYER_DECODE_HW_ACCEL");
    if (val) {
        overrides_["decode.hw_accel"] = std::string(val);
    }

    val = std::getenv("PLAYER_SYNC_MASTER_CLOCK");
    if (val) {
        overrides_["sync.master_clock"] = std::string(val);
    }

    val = std::getenv("PLAYER_LOG_LEVEL");
    if (val) {
        overrides_["log.level"] = std::string(val);
    }
}

PlayerConfig ConfigManager::getConfig() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    PlayerConfig config = base_config_;
    applyOverrides(config);
    return config;
}

void ConfigManager::override(const std::string& key, const std::any& value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    overrides_[key] = value;
}

void ConfigManager::override(const std::map<std::string, std::any>& overrides)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [key, value] : overrides) {
        overrides_[key] = value;
    }
}

void ConfigManager::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    overrides_.clear();
}

void ConfigManager::applyOverrides(PlayerConfig& config) const
{
    // TODO: Walk the overrides map and apply each value to the matching
    // PlayerConfig field. This requires serializing/deserializing each
    // field by its dotted key path.
    //
    // Example:
    //   "source.timeout_ms" -> config.source.timeout_ms = std::any_cast<int>(value)
    //   "render.video.vsync" -> config.render.video.vsync = std::any_cast<bool>(value)
    //
    // For now this is a structural placeholder.
    (void)config;
}

std::string ConfigManager::envToKey(const std::string& env_var) const
{
    // Strip "PLAYER_" prefix, convert to lowercase, replace '_' with '.'
    if (env_var.rfind("PLAYER_", 0) != 0) {
        return {};
    }

    std::string key = env_var.substr(7); // len of "PLAYER_"
    std::transform(key.begin(), key.end(), key.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Replace '_' with '.' for section separators
    // Need smarter heuristics for multi-word keys (e.g., timeout_ms -> timeout.ms)
    // This is a simplistic approach.
    size_t pos = key.find('_');
    while (pos != std::string::npos) {
        key[pos] = '.';
        pos = key.find('_', pos + 1);
    }

    return key;
}

} // namespace player
