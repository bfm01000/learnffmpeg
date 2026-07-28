#pragma once

#include "api/player_config.h"

#include <any>
#include <map>
#include <mutex>
#include <string>

namespace player {

/// @brief 配置管理器
///
/// 从多个数据源加载 PlayerConfig，支持优先级覆盖：
///   1. 程序式设置（优先级最高）
///   2. 环境变量
///   3. JSON 文件（优先级最低）
class ConfigManager {
public:
    ConfigManager();

    // ── 加载 ──────────────────────────────────────────────────────────────

    /// @brief 从 JSON 文件加载配置
    /// @param path JSON 文件路径
    /// @return true 成功，false 文件不存在或解析失败
    bool loadFromFile(const std::string& path);

    /// @brief 从环境变量加载配置
    /// 环境变量命名规则：PLAYER_<SECTION>_<KEY>，如 PLAYER_RENDER_VIDEO_VSYNC=1
    void loadFromEnv();

    // ── 查询 ──────────────────────────────────────────────────────────────

    /// @brief 获取合并后的完整配置
    /// @return 当前 PlayerConfig（含所有数据源的覆盖）
    PlayerConfig getConfig() const;

    // ── 覆盖 ──────────────────────────────────────────────────────────────

    /// @brief 程序式覆盖某个配置项
    /// @param key 点分路径 如 "source.timeout_ms"
    /// @param value 值（支持 int / double / bool / string）
    void override(const std::string& key, const std::any& value);

    /// @brief 批量覆盖
    void override(const std::map<std::string, std::any>& overrides);

    /// @brief 重置所有覆盖
    void reset();

private:
    /// @brief 应用所有覆盖到 config
    void applyOverrides(PlayerConfig& config) const;

    /// @brief 解析环境变量前缀
    std::string envToKey(const std::string& env_var) const;

    PlayerConfig              base_config_;
    std::map<std::string, std::any> overrides_;
    mutable std::mutex              mutex_;
};

} // namespace player
