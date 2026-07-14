#include "sql/server/thread_pool.h"

#include <stdexcept>

namespace goods_db {

ThreadPool::ThreadPool(size_t num_workers) {
  if (num_workers == 0) {
    num_workers = std::thread::hardware_concurrency();
    if (num_workers == 0) num_workers = 4;  // fallback
  }

  workers_.reserve(num_workers);
  for (size_t i = 0; i < num_workers; i++) {
    workers_.emplace_back(&ThreadPool::WorkerLoop, this);
  }
}

ThreadPool::~ThreadPool() {
  if (!stop_) {
    Shutdown();
  }
}

void ThreadPool::WorkerLoop() {
  while (true) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      condition_.wait(lock, [this] {
        return stop_ || !task_queue_.empty();
      });

      if (stop_ && (force_stop_ || task_queue_.empty())) {
        return;
      }

      if (!task_queue_.empty()) {
        task = std::move(task_queue_.front());
        task_queue_.pop();
      }
    }

    if (task) {
      active_tasks_++;
      task();
      active_tasks_--;
    }

    // Notify waiters that a task completed
    finished_condition_.notify_one();
  }
}

void ThreadPool::Shutdown() {
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    stop_ = true;
  }
  condition_.notify_all();

  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

void ThreadPool::ShutdownNow() {
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    stop_ = true;
    force_stop_ = true;
    // Clear pending tasks
    while (!task_queue_.empty()) {
      task_queue_.pop();
    }
  }
  condition_.notify_all();

  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

size_t ThreadPool::GetPendingTaskCount() const {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  return task_queue_.size();
}

}  // namespace goods_db
