#include "playlist_manager.h"

#include <algorithm>
#include <cassert>

namespace player {

PlaylistManager::PlaylistManager()
    : current_index_(-1)
    , loop_mode_(LoopMode::NoLoop)
    , preload_next_ms_(3000)
{
}

size_t PlaylistManager::add(const std::string& url)
{
    if (url.empty()) {
        return urls_.size();
    }

    urls_.push_back(url);

    // If this is the first item, set current to 0
    if (urls_.size() == 1) {
        current_index_ = 0;
    }

    return urls_.size() - 1;
}

bool PlaylistManager::remove(size_t index)
{
    if (index >= urls_.size()) {
        return false;
    }

    urls_.erase(urls_.begin() + static_cast<ptrdiff_t>(index));

    // Adjust current index
    if (urls_.empty()) {
        current_index_ = -1;
    } else if (static_cast<int64_t>(index) < current_index_) {
        --current_index_;
    } else if (static_cast<int64_t>(index) == current_index_) {
        // Current item removed — clamp to new size
        if (current_index_ >= static_cast<int64_t>(urls_.size())) {
            current_index_ = static_cast<int64_t>(urls_.size()) - 1;
        }
    }

    return true;
}

void PlaylistManager::clear()
{
    urls_.clear();
    current_index_ = -1;
}

int64_t PlaylistManager::next()
{
    if (urls_.empty()) {
        return -1;
    }

    if (current_index_ + 1 < static_cast<int64_t>(urls_.size())) {
        ++current_index_;
    } else if (loop_mode_ == LoopMode::LoopAll) {
        current_index_ = 0;
    } else if (loop_mode_ == LoopMode::LoopOne) {
        // Stay on the same item
        return current_index_;
    } else {
        // NoLoop and at end — no next item
        return -1;
    }

    return current_index_;
}

int64_t PlaylistManager::prev()
{
    if (urls_.empty()) {
        return -1;
    }

    if (current_index_ - 1 >= 0) {
        --current_index_;
    } else if (loop_mode_ == LoopMode::LoopAll) {
        current_index_ = static_cast<int64_t>(urls_.size()) - 1;
    } else if (loop_mode_ == LoopMode::LoopOne) {
        return current_index_;
    } else {
        // NoLoop and at beginning — no prev item
        return -1;
    }

    return current_index_;
}

std::string PlaylistManager::current() const
{
    if (current_index_ < 0 || current_index_ >= static_cast<int64_t>(urls_.size())) {
        return {};
    }
    return urls_[static_cast<size_t>(current_index_)];
}

bool PlaylistManager::shouldPreload(int64_t position_ms) const
{
    if (urls_.empty() || preload_next_ms_ <= 0) {
        return false;
    }

    int64_t next_idx = current_index_ + 1;
    return loop_mode_ == LoopMode::LoopAll
        || (next_idx >= 0 && next_idx < static_cast<int64_t>(urls_.size()));
}

} // namespace player
