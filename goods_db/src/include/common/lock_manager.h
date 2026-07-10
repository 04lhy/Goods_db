#pragma once

#include <condition_variable>
#include <mutex>
#include <unordered_map>

#include "common/rid.h"

namespace goods_db {

/**
 * LockManager — simple row-level lock manager.
 *
 * Supports shared (S) and exclusive (X) locks.
 * Implements basic deadlock-free locking with FIFO waiting.
 */
class LockManager {
public:
    enum class LockMode { SHARED = 0, EXCLUSIVE = 1 };

    LockManager() = default;
    ~LockManager() = default;

    /** Acquire a lock on a RID. Blocks until granted. */
    bool Lock(const RID& rid, LockMode mode);

    /** Release a lock on a RID */
    bool Unlock(const RID& rid);

    /** Check if a lock is held */
    bool IsLocked(const RID& rid) const;

private:
    struct LockEntry {
        LockMode mode{LockMode::SHARED};
        int count{0};  // number of holders
    };

    mutable std::mutex mutex_;
    std::unordered_map<RID, LockEntry, RID::Hash> locks_;
    std::condition_variable cv_;
};

}  // namespace goods_db
