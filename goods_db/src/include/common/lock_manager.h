#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/rid.h"

namespace goods_db {

/**
 * LockManager — full-featured lock manager with:
 *   - Row-level S/X locks
 *   - Table-level intention locks (IS/IX/SIX/X)
 *   - Deadlock detection via waits-for graph + DFS
 *   - Lock wait timeout
 *   - Statistics for monitoring
 */
class LockManager {
public:
    enum class LockMode : uint8_t {
        SHARED = 0,          // S  — row-level shared
        EXCLUSIVE = 1,       // X  — row-level exclusive
        INTENTION_SHARED = 2,     // IS — table-level intention shared
        INTENTION_EXCLUSIVE = 3,  // IX — table-level intention exclusive
        SHARED_INTENTION_EXCLUSIVE = 4,  // SIX — table-level S + IX
    };

    /** Convert LockMode to human-readable string */
    static const char* LockModeToString(LockMode mode);

    LockManager();
    ~LockManager();

    // ---- Row-level locking ---------------------------------------------------

    /**
     * Acquire a row-level lock on a RID.
     * @param rid The row identifier
     * @param mode S or X
     * @param timeout_ms Max wait time in milliseconds (0 = no wait, -1 = infinite)
     * @return true if lock was granted, false on timeout or deadlock
     */
    bool LockRow(const RID& rid, LockMode mode,
                 int64_t timeout_ms = -1);

    /** Release a row-level lock */
    bool UnlockRow(const RID& rid);

    // ---- Table-level locking -------------------------------------------------

    /**
     * Acquire a table-level intention lock.
     * @param table_name The table name (used as lock key)
     * @param mode IS/IX/SIX/X
     * @param timeout_ms Max wait time in milliseconds
     * @return true if lock was granted
     */
    bool LockTable(const std::string& table_name, LockMode mode,
                   int64_t timeout_ms = -1);

    /** Release a table-level lock */
    bool UnlockTable(const std::string& table_name);

    // ---- Bulk operations -----------------------------------------------------

    /** Release all locks held by a transaction */
    void ReleaseAllLocks(uint64_t txn_id);

    // ---- Deadlock detection --------------------------------------------------

    /** Check if granting `mode` on `rid` to `txn_id` would cause a deadlock */
    bool WouldDeadlock(uint64_t txn_id, const RID& rid, LockMode mode);

    /** Get the number of deadlocks detected (for statistics) */
    uint64_t GetDeadlockCount() const { return deadlock_count_.load(); }

    // ---- Statistics ----------------------------------------------------------

    size_t GetLockCount() const;
    size_t GetRowLockCount() const;
    size_t GetTableLockCount() const;
    double GetAvgWaitTimeMs() const;

private:
    // Per-lock entry state
    struct LockEntry {
        LockMode mode{LockMode::SHARED};
        int count{0};  // number of S holders (for row locks)
        uint64_t txn_id{0};  // owning transaction (for X locks)
        std::vector<uint64_t> waiting_txns;  // transactions waiting on this lock
    };

    // Per-transaction lock tracking (for deadlock detection)
    struct TxnLockInfo {
        std::vector<RID> row_locks;
        std::vector<std::string> table_locks;
        std::chrono::steady_clock::time_point last_wait_start;
    };

    // Row locks: RID → LockEntry
    mutable std::mutex row_mutex_;
    std::unordered_map<RID, LockEntry, RID::Hash> row_locks_;
    std::condition_variable row_cv_;

    // Table locks: table_name → LockEntry
    mutable std::mutex table_mutex_;
    std::unordered_map<std::string, LockEntry> table_locks_;
    std::condition_variable table_cv_;

    // Transaction lock tracking
    mutable std::mutex txn_mutex_;
    std::unordered_map<uint64_t, TxnLockInfo> txn_locks_;

    // Deadlock statistics
    std::atomic<uint64_t> deadlock_count_{0};

    // Wait statistics
    mutable std::mutex stats_mutex_;
    uint64_t total_wait_time_us_{0};
    uint64_t total_wait_count_{0};

    // ---- Internal helpers ----------------------------------------------------

    /** Check for lock mode compatibility */
    static bool ModesCompatible(LockMode held, LockMode requested);

    /** Build waits-for graph and run DFS to detect cycles (returns true if deadlock) */
    bool DetectDeadlock(uint64_t requesting_txn);

    /** Record a lock grant for deadlock tracking */
    void RecordLockGrant(uint64_t txn_id, const RID& rid, LockMode mode);
    void RecordTableLockGrant(uint64_t txn_id, const std::string& table_name);
    void RecordLockRelease(uint64_t txn_id, const RID& rid);
    void RecordTableLockRelease(uint64_t txn_id, const std::string& table_name);
};

}  // namespace goods_db
