#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace goods_db {

// =============================================================================
// ThreadPool — fixed-size pool of worker threads
//
// Tasks are submitted via Submit() which returns a std::future for the result.
// Supports graceful shutdown (Shutdown — waits for all pending tasks) and
// forced shutdown (ShutdownNow — drops unexecuted tasks).
// =============================================================================
class ThreadPool {
 public:
  // num_workers = 0  →  auto-detect (std::thread::hardware_concurrency())
  explicit ThreadPool(size_t num_workers = 0);
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  // Submit a callable and get a future for its result.
  template <typename Func, typename... Args>
  auto Submit(Func&& func, Args&&... args)
      -> std::future<decltype(func(args...))>;

  // Graceful shutdown: wait for all queued tasks to finish.
  void Shutdown();

  // Force shutdown: discard unexecuted tasks, interrupt workers.
  void ShutdownNow();

  size_t GetWorkerCount() const { return workers_.size(); }
  size_t GetPendingTaskCount() const;
  size_t GetActiveTaskCount() const { return active_tasks_.load(); }

 private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> task_queue_;
  mutable std::mutex queue_mutex_;
  std::condition_variable condition_;
  std::condition_variable finished_condition_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> force_stop_{false};
  std::atomic<size_t> active_tasks_{0};

  void WorkerLoop();
};

// ---- Template implementation -----------------------------------------------

template <typename Func, typename... Args>
auto ThreadPool::Submit(Func&& func, Args&&... args)
    -> std::future<decltype(func(args...))> {
  using ReturnType = decltype(func(args...));

  auto task = std::make_shared<std::packaged_task<ReturnType()>>(
      std::bind(std::forward<Func>(func), std::forward<Args>(args)...));

  std::future<ReturnType> result = task->get_future();

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (stop_) {
      throw std::runtime_error("ThreadPool is stopped");
    }
    task_queue_.emplace([task]() { (*task)(); });
  }

  condition_.notify_one();
  return result;
}

}  // namespace goods_db
