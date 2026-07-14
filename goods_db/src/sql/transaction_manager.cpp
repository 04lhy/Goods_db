#include "common/transaction_manager.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace goods_db {

// Thread-local pointer to the currently active transaction for this thread
thread_local Transaction* TransactionManager::current_txn_ = nullptr;

TransactionManager::TransactionManager() = default;

TransactionManager::~TransactionManager() {
    // Persist XID before destruction
    PersistXid();

    // Clean up any remaining active transactions
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto* txn : active_txns_) {
        txn->state = Transaction::State::ABORTED;
        delete txn;
    }
    active_txns_.clear();
}

void TransactionManager::Initialize(const std::string& xid_file) {
    if (!xid_file.empty()) {
        xid_file_ = xid_file;
        LoadXid(xid_file);
    }
    // Ensure we start at least from 1
    if (last_xid_.load() == 0) {
        last_xid_.store(1);
    }
}

// =============================================================================
// Transaction lifecycle
// =============================================================================

Transaction* TransactionManager::Begin() {
    return Begin(default_isolation_level_);
}

Transaction* TransactionManager::Begin(Transaction::IsolationLevel level) {
    uint64_t xid = AllocateXid();
    auto* txn = new Transaction(xid);
    txn->state = Transaction::State::ACTIVE;
    txn->isolation_level = level;
    txn->start_ts = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_txns_.push_back(txn);
    }

    SetCurrentTransaction(txn);
    total_txns_started_++;
    return txn;
}

bool TransactionManager::Commit(Transaction* txn) {
    if (!txn || txn->state != Transaction::State::ACTIVE) return false;

    txn->state = Transaction::State::COMMITTED;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find(active_txns_.begin(), active_txns_.end(), txn);
        if (it != active_txns_.end()) {
            active_txns_.erase(it);
        }
    }

    // Persist XID on commit for durability
    PersistXid();

    total_txns_committed_++;

    if (current_txn_ == txn) {
        current_txn_ = nullptr;
    }
    delete txn;
    return true;
}

bool TransactionManager::Rollback(Transaction* txn) {
    if (!txn || txn->state != Transaction::State::ACTIVE) return false;

    txn->state = Transaction::State::ABORTED;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find(active_txns_.begin(), active_txns_.end(), txn);
        if (it != active_txns_.end()) {
            active_txns_.erase(it);
        }
    }

    total_txns_rolled_back_++;

    if (current_txn_ == txn) {
        current_txn_ = nullptr;
    }
    delete txn;
    return true;
}

// =============================================================================
// Savepoint operations
// =============================================================================

bool TransactionManager::CreateSavepoint(const std::string& name) {
    Transaction* txn = GetCurrentTransaction();
    if (!txn) return false;

    // Check for duplicate savepoint name
    for (const auto& sp : txn->savepoints) {
        if (sp.name == name) {
            // Replace existing savepoint with same name (MySQL behavior)
            // We'll just add a new one; ReleaseSavepoint handles removal
            break;
        }
    }

    Savepoint sp;
    sp.name = name;
    sp.txn_id = txn->txn_id;
    sp.created_at = static_cast<int64_t>(txn->savepoints.size());
    txn->savepoints.push_back(sp);
    return true;
}

bool TransactionManager::ReleaseSavepoint(const std::string& name) {
    Transaction* txn = GetCurrentTransaction();
    if (!txn) return false;

    auto it = std::find_if(txn->savepoints.begin(), txn->savepoints.end(),
        [&](const Savepoint& sp) { return sp.name == name; });
    if (it == txn->savepoints.end()) return false;

    txn->savepoints.erase(it);
    return true;
}

bool TransactionManager::RollbackToSavepoint(const std::string& name) {
    Transaction* txn = GetCurrentTransaction();
    if (!txn) return false;

    // Find the savepoint position
    auto it = std::find_if(txn->savepoints.begin(), txn->savepoints.end(),
        [&](const Savepoint& sp) { return sp.name == name; });
    if (it == txn->savepoints.end()) return false;

    // Remove all savepoints created after this one (including this one is kept;
    // MySQL removes savepoints after the target)
    // We keep the target savepoint and remove all after it
    txn->savepoints.erase(it + 1, txn->savepoints.end());

    // In a full implementation, this would also undo any changes made
    // after the savepoint was created, using the undo log.

    return true;
}

// =============================================================================
// Transaction access
// =============================================================================

Transaction* TransactionManager::GetCurrentTransaction() {
    return current_txn_;
}

void TransactionManager::SetCurrentTransaction(Transaction* txn) {
    current_txn_ = txn;
}

size_t TransactionManager::GetActiveTransactionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_txns_.size();
}

// =============================================================================
// Isolation level
// =============================================================================

void TransactionManager::SetDefaultIsolationLevel(Transaction::IsolationLevel level) {
    default_isolation_level_ = level;
}

// =============================================================================
// XID management
// =============================================================================

uint64_t TransactionManager::AllocateXid() {
    uint64_t xid = last_xid_.fetch_add(1) + 1;

    // Periodically persist XID (every 1000 allocations)
    if (xid % 1000 == 0 && !xid_file_.empty()) {
        WriteXidFile(xid);
    }

    return xid;
}

bool TransactionManager::PersistXid() {
    if (xid_file_.empty()) return true;
    return WriteXidFile(last_xid_.load());
}

bool TransactionManager::LoadXid(const std::string& xid_file) {
    std::ifstream file(xid_file);
    if (!file.is_open()) {
        // File doesn't exist yet — start from 1
        last_xid_.store(1);
        return true;
    }

    uint64_t xid = 0;
    file >> xid;
    if (xid > 0) {
        last_xid_.store(xid);
    } else {
        last_xid_.store(1);
    }
    return true;
}

bool TransactionManager::WriteXidFile(uint64_t xid) {
    std::ofstream file(xid_file_);
    if (!file.is_open()) return false;
    file << xid;
    file.close();
    return true;
}

}  // namespace goods_db
