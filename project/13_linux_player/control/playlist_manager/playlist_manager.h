#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace player {

/// @brief 循环模式
enum class LoopMode {
    NoLoop,       ///< 播放列表播完即止
    LoopAll,      ///< 列表循环（播完从头开始）
    LoopOne,      ///< 单曲循环
};

/// @brief 播放列表管理器
///
/// 维护一个 URL 列表，支持增删改查和循环模式。
class PlaylistManager {
public:
    PlaylistManager();

    // ── 列表操作 ──────────────────────────────────────────────────────────

    /// @brief 添加 URL 到列表末尾
    /// @return 添加后的索引
    size_t add(const std::string& url);

    /// @brief 移除指定索引的项
    /// @return true 成功，false 索引无效
    bool remove(size_t index);

    /// @brief 清空列表
    void clear();

    // ── 导航 ──────────────────────────────────────────────────────────────

    /// @brief 切换到下一首
    /// @return 当前索引，-1 表示无下一首
    int64_t next();

    /// @brief 切换到上一首
    /// @return 当前索引，-1 表示无上一首
    int64_t prev();

    /// @brief 获取当前项的 URL
    /// @return URL 字符串，空串表示无内容
    std::string current() const;

    // ── 查询 ──────────────────────────────────────────────────────────────

    /// @brief 获取列表大小
    size_t size() const { return urls_.size(); }

    /// @brief 获取当前索引
    int64_t currentIndex() const { return current_index_; }

    /// @brief 判断列表是否为空
    bool empty() const { return urls_.empty(); }

    // ── 配置 ──────────────────────────────────────────────────────────────

    /// @brief 设置循环模式
    void setLoop(LoopMode mode) { loop_mode_ = mode; }

    /// @brief 获取循环模式
    LoopMode getLoop() const { return loop_mode_; }

    /// @brief 设置预加载下一首的提前量（毫秒）
    void setPreloadNext(int64_t ms) { preload_next_ms_ = ms; }

    /// @brief 检查是否需要在指定位置前预加载下一首
    bool shouldPreload(int64_t position_ms) const;

private:
    std::vector<std::string> urls_;
    int64_t current_index_;
    LoopMode loop_mode_;
    int64_t preload_next_ms_;  ///< 提前预加载阈值（毫秒）
};

} // namespace player
