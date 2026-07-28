/// @file thread_pool.cpp
/// @brief ThreadPool — 固定大小线程池实现.

#include "core/thread/thread_pool.h"

namespace player {

ThreadPool::ThreadPool(size_t numThreads) {
  if (numThreads == 0) {
    numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;  // fallback
  }

  m_workers.reserve(numThreads);
  for (size_t i = 0; i < numThreads; ++i) {
    m_workers.emplace_back([this] { workerLoop_(); });
  }
}

ThreadPool::~ThreadPool() {
  shutdown();
}

void ThreadPool::enqueueTask(Task task) {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stopped) {
      throw std::runtime_error("ThreadPool: enqueueTask on stopped pool");
    }
    m_tasks.emplace(std::move(task));
  }
  m_condition.notify_one();
}

size_t ThreadPool::pendingTasks() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_tasks.size();
}

void ThreadPool::waitAll() {
  // 简单自旋等待 — 适用于短暂等待场景
  while (true) {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_tasks.empty()) break;
    }
    std::this_thread::yield();
  }
}

void ThreadPool::shutdown() {
  m_stopped.store(true, std::memory_order_release);
  m_condition.notify_all();

  for (auto& worker : m_workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  m_workers.clear();
}

void ThreadPool::workerLoop_() {
  while (true) {
    Task task;
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_condition.wait(lock, [this] {
        return m_stopped.load(std::memory_order_acquire) || !m_tasks.empty();
      });

      if (m_stopped.load(std::memory_order_acquire) && m_tasks.empty()) {
        return;
      }

      task = std::move(m_tasks.front());
      m_tasks.pop();
    }

    if (task) {
      task();
    }
  }
}

} // namespace player
