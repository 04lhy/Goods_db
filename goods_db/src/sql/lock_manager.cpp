#include "common/lock_manager.h"

#include <thread>

namespace goods_db {

bool LockManager::Lock(const RID& rid, LockMode mode) {
    std::unique_lock<std::mutex> lock(mutex_);

    // Simple implementation: grant lock if no conflict
    // In production: use wait queue with deadlock detection
    auto it = locks_.find(rid);
    if (it == locks_.end()) {
        locks_[rid] = {mode, 1};
        return true;
    }

    // Allow multiple shared locks
    if (mode == LockMode::SHARED && it->second.mode == LockMode::SHARED) {
        it->second.count++;
        return true;
    }

    // Conflict — wait (simplified: busy-wait with yield)
    // In production, use condition variable with proper wait queues
    while (it != locks_.end() &&
           (mode == LockMode::EXCLUSIVE || it->second.mode == LockMode::EXCLUSIVE)) {
        lock.unlock();
        std::this_thread::yield();
        lock.lock();
        it = locks_.find(rid);
    }

    if (it == locks_.end()) {
        locks_[rid] = {mode, 1};
    } else {
        it->second.count++;
    }
    return true;
}

bool LockManager::Unlock(const RID& rid) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = locks_.find(rid);
    if (it == locks_.end()) return false;

    it->second.count--;
    if (it->second.count <= 0) {
        locks_.erase(it);
    }
    cv_.notify_all();
    return true;
}

bool LockManager::IsLocked(const RID& rid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return locks_.find(rid) != locks_.end();
}

}  // namespace goods_db
