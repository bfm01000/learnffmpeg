#pragma once

/// @file thread_pool.h
/// @brief 固定大小线程池。用于批量异步任务（未来被 PlayerController 的专用线程替代）。

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace player {

class ThreadPool {
public:
  using Task = std::function<void()>;

  /// @param numThreads 工作线程数, 0 = auto (hardware_concurrency)
  explicit ThreadPool(size_t numThreads = 0);
  ~ThreadPool();

  /// 提交带返回值的任务
  template <typename F, typename... Args>
  auto enqueue(F&& f, Args&&... args)
      -> std::future<typename std::invoke_result_t<F, Args...>> {
    using ReturnType = typename std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<ReturnType> result = task->get_future();

    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_stopped) {
        throw std::runtime_error("ThreadPool: enqueue on stopped pool");
      }
      m_tasks.emplace([task] { (*task)(); });
    }
    m_condition.notify_one();
    return result;
  }

  /// 提交无返回值任务
  void enqueueTask(Task task);

  size_t workerCount()   const { return m_workers.size(); }
  size_t pendingTasks()  const;

  /// 等待所有已提交任务完成
  void waitAll();

  /// 停止并 join 所有线程
  void shutdown();

private:
  void workerLoop_();

  std::vector<std::thread>    m_workers;
  std::queue<Task>            m_tasks;
  mutable std::mutex          m_mutex;
  std::condition_variable     m_condition;
  std::atomic<bool>           m_stopped{false};
};

} // namespace player
