#include "common/lock_manager.h"

#include <algorithm>
#include <thread>

namespace goods_db {

// =============================================================================
// LockMode helpers
// =============================================================================

const char* LockManager::LockModeToString(LockMode mode) {
    switch (mode) {
        case LockMode::SHARED:                    return "S";
        case LockMode::EXCLUSIVE:                 return "X";
        case LockMode::INTENTION_SHARED:          return "IS";
        case LockMode::INTENTION_EXCLUSIVE:       return "IX";
        case LockMode::SHARED_INTENTION_EXCLUSIVE: return "SIX";
        default:                                   return "UNKNOWN";
    }
}

bool LockManager::ModesCompatible(LockMode held, LockMode requested) {
    // Compatibility matrix (held vs requested):
    //     | S  | X  | IS | IX | SIX
    // S   | Y  | N  | Y  | N  | N
    // X   | N  | N  | N  | N  | N
    // IS  | Y  | N  | Y  | Y  | Y
    // IX  | N  | N  | Y  | Y  | N
    // SIX | N  | N  | Y  | N  | N
    switch (held) {
        case LockMode::SHARED:
            return requested == LockMode::SHARED || requested == LockMode::INTENTION_SHARED;
        case LockMode::EXCLUSIVE:
            return false;  // X conflicts with everything
        case LockMode::INTENTION_SHARED:
            return requested != LockMode::EXCLUSIVE;
        case LockMode::INTENTION_EXCLUSIVE:
            return requested == LockMode::INTENTION_SHARED ||
                   requested == LockMode::INTENTION_EXCLUSIVE;
        case LockMode::SHARED_INTENTION_EXCLUSIVE:
            return requested == LockMode::INTENTION_SHARED;
    }
    return false;
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

LockManager::LockManager() = default;
LockManager::~LockManager() = default;

// =============================================================================
// Row-level locking
// =============================================================================

bool LockManager::LockRow(const RID& rid, LockMode mode, int64_t timeout_ms) {
    uint64_t txn_id = 0;  // In a full implementation, get from thread-local current txn

    std::unique_lock<std::mutex> lock(row_mutex_);

    auto it = row_locks_.find(rid);
    if (it == row_locks_.end()) {
        // No existing lock — grant immediately
        LockEntry entry;
        entry.mode = mode;
        entry.count = 1;
        entry.txn_id = txn_id;
        row_locks_[rid] = entry;
        RecordLockGrant(txn_id, rid, mode);
        return true;
    }

    LockEntry& entry = it->second;

    // Check compatibility
    if (ModesCompatible(entry.mode, mode)) {
        // Compatible: multiple S locks allowed
        if (mode == LockMode::SHARED && entry.mode == LockMode::SHARED) {
            entry.count++;
            RecordLockGrant(txn_id, rid, mode);
            return true;
        }
    }

    // Deadlock detection
    if (WouldDeadlock(txn_id, rid, mode)) {
        deadlock_count_++;
        return false;  // Would cause deadlock, abort
    }

    // Wait for lock
    auto wait_start = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        total_wait_count_++;
    }

    bool granted = false;
    if (timeout_ms == 0) {
        // No wait
        return false;
    } else if (timeout_ms < 0) {
        // Infinite wait
        entry.waiting_txns.push_back(txn_id);
        row_cv_.wait(lock, [&]() {
            auto it2 = row_locks_.find(rid);
            if (it2 == row_locks_.end()) return true;
            return ModesCompatible(it2->second.mode, mode) &&
                   (mode == LockMode::SHARED || it2->second.count == 0);
        });

        // Remove from wait queue
        auto& wq = row_locks_[rid].waiting_txns;
        wq.erase(std::remove(wq.begin(), wq.end(), txn_id), wq.end());

        granted = true;
    } else {
        // Timed wait
        entry.waiting_txns.push_back(txn_id);
        granted = row_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&]() {
            auto it2 = row_locks_.find(rid);
            if (it2 == row_locks_.end()) return true;
            return ModesCompatible(it2->second.mode, mode) &&
                   (mode == LockMode::SHARED || it2->second.count == 0);
        });

        // Remove from wait queue
        auto it_find = row_locks_.find(rid);
        if (it_find != row_locks_.end()) {
            auto& wq = it_find->second.waiting_txns;
            wq.erase(std::remove(wq.begin(), wq.end(), txn_id), wq.end());
        }
    }

    // Update wait statistics
    auto wait_end = std::chrono::steady_clock::now();
    auto wait_us = std::chrono::duration_cast<std::chrono::microseconds>(
        wait_end - wait_start).count();
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        total_wait_time_us_ += static_cast<uint64_t>(wait_us);
    }

    if (!granted) {
        return false;  // Timeout
    }

    // Grant the lock
    auto it3 = row_locks_.find(rid);
    if (it3 == row_locks_.end()) {
        LockEntry new_entry;
        new_entry.mode = mode;
        new_entry.count = 1;
        new_entry.txn_id = txn_id;
        row_locks_[rid] = new_entry;
    } else {
        it3->second.count++;
    }
    RecordLockGrant(txn_id, rid, mode);

    return true;
}

bool LockManager::UnlockRow(const RID& rid) {
    uint64_t txn_id = 0;
    std::lock_guard<std::mutex> lock(row_mutex_);

    auto it = row_locks_.find(rid);
    if (it == row_locks_.end()) return false;

    it->second.count--;
    if (it->second.count <= 0) {
        row_locks_.erase(it);
    }

    RecordLockRelease(txn_id, rid);
    row_cv_.notify_all();
    return true;
}

// =============================================================================
// Table-level locking
// =============================================================================

bool LockManager::LockTable(const std::string& table_name, LockMode mode,
                            int64_t timeout_ms) {
    uint64_t txn_id = 0;
    auto wait_start = std::chrono::steady_clock::now();

    std::unique_lock<std::mutex> lock(table_mutex_);

    auto it = table_locks_.find(table_name);
    if (it == table_locks_.end()) {
        LockEntry entry;
        entry.mode = mode;
        entry.count = 1;
        entry.txn_id = txn_id;
        table_locks_[table_name] = entry;
        RecordTableLockGrant(txn_id, table_name);
        return true;
    }

    if (ModesCompatible(it->second.mode, mode)) {
        it->second.count++;
        RecordTableLockGrant(txn_id, table_name);
        return true;
    }

    // Wait
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        total_wait_count_++;
    }

    bool granted = false;
    if (timeout_ms < 0) {
        table_cv_.wait(lock, [&]() {
            auto it2 = table_locks_.find(table_name);
            if (it2 == table_locks_.end()) return true;
            return ModesCompatible(it2->second.mode, mode);
        });
        granted = true;
    } else if (timeout_ms > 0) {
        granted = table_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&]() {
            auto it2 = table_locks_.find(table_name);
            if (it2 == table_locks_.end()) return true;
            return ModesCompatible(it2->second.mode, mode);
        });
    }

    auto wait_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - wait_start).count();
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        total_wait_time_us_ += static_cast<uint64_t>(wait_us);
    }

    if (!granted) return false;

    // Grant
    auto it_final = table_locks_.find(table_name);
    if (it_final == table_locks_.end()) {
        LockEntry new_entry;
        new_entry.mode = mode;
        new_entry.count = 1;
        table_locks_[table_name] = new_entry;
    } else {
        it_final->second.count++;
    }
    RecordTableLockGrant(txn_id, table_name);
    return true;
}

bool LockManager::UnlockTable(const std::string& table_name) {
    uint64_t txn_id = 0;
    std::lock_guard<std::mutex> lock(table_mutex_);

    auto it = table_locks_.find(table_name);
    if (it == table_locks_.end()) return false;

    it->second.count--;
    if (it->second.count <= 0) {
        table_locks_.erase(it);
    }

    RecordTableLockRelease(txn_id, table_name);
    table_cv_.notify_all();
    return true;
}

// =============================================================================
// Bulk release
// =============================================================================

void LockManager::ReleaseAllLocks(uint64_t txn_id) {
    {
        std::lock_guard<std::mutex> lock(row_mutex_);
        auto it = txn_locks_.find(txn_id);
        if (it != txn_locks_.end()) {
            for (const auto& rid : it->second.row_locks) {
                auto rit = row_locks_.find(rid);
                if (rit != row_locks_.end()) {
                    rit->second.count--;
                    if (rit->second.count <= 0) {
                        row_locks_.erase(rit);
                    }
                }
            }
            for (const auto& tbl : it->second.table_locks) {
                auto tit = table_locks_.find(tbl);
                if (tit != table_locks_.end()) {
                    tit->second.count--;
                    if (tit->second.count <= 0) {
                        table_locks_.erase(tit);
                    }
                }
            }
            txn_locks_.erase(it);
        }
    }
    row_cv_.notify_all();
    table_cv_.notify_all();
}

// =============================================================================
// Deadlock detection — waits-for graph + DFS cycle detection
// =============================================================================

bool LockManager::WouldDeadlock(uint64_t txn_id, const RID& rid, LockMode mode) {
    return DetectDeadlock(txn_id);
}

bool LockManager::DetectDeadlock(uint64_t requesting_txn) {
    // Build waits-for graph:
    // Nodes = active transactions
    // Edge A→B if A is waiting for a lock held by B
    std::unordered_map<uint64_t, std::vector<uint64_t>> graph;

    {
        std::lock_guard<std::mutex> lock(row_mutex_);
        for (const auto& [r, entry] : row_locks_) {
            if (entry.txn_id != 0) {
                for (uint64_t waiter : entry.waiting_txns) {
                    graph[waiter].push_back(entry.txn_id);
                }
            }
        }
    }

    // DFS cycle detection using coloring: 0=white, 1=gray, 2=black
    std::unordered_map<uint64_t, int> color;
    std::function<bool(uint64_t)> dfs = [&](uint64_t node) -> bool {
        color[node] = 1;  // gray — in current path
        for (uint64_t neighbor : graph[node]) {
            if (color.find(neighbor) == color.end() || color[neighbor] == 0) {
                if (dfs(neighbor)) return true;
            } else if (color[neighbor] == 1) {
                return true;  // Back edge → cycle → deadlock
            }
        }
        color[node] = 2;  // black — fully explored
        return false;
    };

    // Check for cycles starting from each unvisited node
    for (const auto& [node, _] : graph) {
        if (color.find(node) == color.end() || color[node] == 0) {
            if (dfs(node)) return true;
        }
    }

    return false;
}

// =============================================================================
// Lock tracking for deadlock detection
// =============================================================================

void LockManager::RecordLockGrant(uint64_t txn_id, const RID& rid, LockMode /*mode*/) {
    if (txn_id == 0) return;
    std::lock_guard<std::mutex> lock(txn_mutex_);
    txn_locks_[txn_id].row_locks.push_back(rid);
}

void LockManager::RecordTableLockGrant(uint64_t txn_id, const std::string& table_name) {
    if (txn_id == 0) return;
    std::lock_guard<std::mutex> lock(txn_mutex_);
    txn_locks_[txn_id].table_locks.push_back(table_name);
}

void LockManager::RecordLockRelease(uint64_t txn_id, const RID& rid) {
    if (txn_id == 0) return;
    std::lock_guard<std::mutex> lock(txn_mutex_);
    auto it = txn_locks_.find(txn_id);
    if (it != txn_locks_.end()) {
        auto& rl = it->second.row_locks;
        rl.erase(std::remove(rl.begin(), rl.end(), rid), rl.end());
    }
}

void LockManager::RecordTableLockRelease(uint64_t txn_id, const std::string& table_name) {
    if (txn_id == 0) return;
    std::lock_guard<std::mutex> lock(txn_mutex_);
    auto it = txn_locks_.find(txn_id);
    if (it != txn_locks_.end()) {
        auto& tl = it->second.table_locks;
        tl.erase(std::remove(tl.begin(), tl.end(), table_name), tl.end());
    }
}

// =============================================================================
// Statistics
// =============================================================================

size_t LockManager::GetLockCount() const {
    return GetRowLockCount() + GetTableLockCount();
}

size_t LockManager::GetRowLockCount() const {
    std::lock_guard<std::mutex> lock(row_mutex_);
    return row_locks_.size();
}

size_t LockManager::GetTableLockCount() const {
    std::lock_guard<std::mutex> lock(table_mutex_);
    return table_locks_.size();
}

double LockManager::GetAvgWaitTimeMs() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (total_wait_count_ == 0) return 0.0;
    return static_cast<double>(total_wait_time_us_) / total_wait_count_ / 1000.0;
}

}  // namespace goods_db
