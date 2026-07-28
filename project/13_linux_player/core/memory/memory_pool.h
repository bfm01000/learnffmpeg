#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>

namespace player {

template <typename T>
class MemoryPool {
public:
    // Pre-allocate a fixed number of objects
    explicit MemoryPool(size_t pool_size = 64)
        : capacity_(pool_size)
    {
        pool_.reserve(pool_size);
        for (size_t i = 0; i < pool_size; ++i) {
            pool_.push_back(std::make_unique<T>());
            available_indices_.push_back(i);
        }
    }

    ~MemoryPool() = default;

    // Acquire an object from the pool (nullptr if pool exhausted)
    T* acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (available_indices_.empty()) {
            return nullptr;
        }
        size_t idx = available_indices_.back();
        available_indices_.pop_back();
        in_use_.store(in_use_.load() + 1);
        return pool_[idx].get();
    }

    // Return an object to the pool
    void release(T* obj) {
        if (!obj) return;

        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < pool_.size(); ++i) {
            if (pool_[i].get() == obj) {
                // Reset the object to default state
                if constexpr (std::is_default_constructible_v<T>) {
                    *pool_[i] = T();
                }
                available_indices_.push_back(i);
                in_use_.store(in_use_.load() - 1);
                return;
            }
        }
    }

    // Get number of free objects
    size_t available() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return available_indices_.size();
    }

    // Get number of objects currently in use
    size_t inUse() const {
        return in_use_.load();
    }

    // Get total pool capacity
    size_t capacity() const { return capacity_; }

    // Expand the pool by a given number of objects
    void expand(size_t count) {
        std::lock_guard<std::mutex> lock(mutex_);
        pool_.reserve(pool_.size() + count);
        for (size_t i = 0; i < count; ++i) {
            pool_.push_back(std::make_unique<T>());
            available_indices_.push_back(pool_.size() - 1);
        }
        capacity_ = pool_.size();
    }

    // Reset the pool (release all objects)
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        available_indices_.clear();
        for (size_t i = 0; i < pool_.size(); ++i) {
            if constexpr (std::is_default_constructible_v<T>) {
                *pool_[i] = T();
            }
            available_indices_.push_back(i);
        }
        in_use_.store(0);
    }

private:
    size_t capacity_;
    std::vector<std::unique_ptr<T>> pool_;
    std::vector<size_t> available_indices_;
    mutable std::mutex mutex_;
    std::atomic<size_t> in_use_{0};
};

} // namespace player
