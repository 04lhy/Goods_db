#pragma once

#include <atomic>
#include <shared_mutex>
#include <thread>

namespace goods_db {

/**
 * Reader-Writer Latch for page-level concurrency control.
 * Based on std::shared_mutex (C++17).
 *
 * Usage:
 *   RWLatch latch;
 *   latch.RLock();   // shared/read lock
 *   latch.RUnlock();
 *   latch.WLock();   // exclusive/write lock
 *   latch.WUnlock();
 */
class RWLatch {
public:
    RWLatch() = default;
    ~RWLatch() = default;

    // Non-copyable, non-movable
    RWLatch(const RWLatch&) = delete;
    RWLatch& operator=(const RWLatch&) = delete;
    RWLatch(RWLatch&&) = delete;
    RWLatch& operator=(RWLatch&&) = delete;

    /** Acquire a shared (read) lock */
    void RLock() { mutex_.lock_shared(); }

    /** Release a shared (read) lock */
    void RUnlock() { mutex_.unlock_shared(); }

    /** Acquire an exclusive (write) lock */
    void WLock() { mutex_.lock(); }

    /** Release an exclusive (write) lock */
    void WUnlock() { mutex_.unlock(); }

    /** Try to acquire a shared lock (non-blocking). Returns true on success. */
    bool TryRLock() { return mutex_.try_lock_shared(); }

    /** Try to acquire an exclusive lock (non-blocking). Returns true on success. */
    bool TryWLock() { return mutex_.try_lock(); }

private:
    std::shared_mutex mutex_;
};

}  // namespace goods_db
