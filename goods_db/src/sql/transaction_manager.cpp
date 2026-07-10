#include "common/transaction_manager.h"

#include <algorithm>

namespace goods_db {

Transaction* TransactionManager::Begin() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* txn = new Transaction();
    active_txns_.push_back(txn);
    return txn;
}

bool TransactionManager::Commit(Transaction* txn) {
    if (!txn || txn->state != Transaction::State::ACTIVE) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    txn->state = Transaction::State::COMMITTED;

    // Remove from active list
    auto it = std::find(active_txns_.begin(), active_txns_.end(), txn);
    if (it != active_txns_.end()) {
        active_txns_.erase(it);
    }
    delete txn;
    return true;
}

bool TransactionManager::Rollback(Transaction* txn) {
    if (!txn || txn->state != Transaction::State::ACTIVE) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    txn->state = Transaction::State::ABORTED;

    auto it = std::find(active_txns_.begin(), active_txns_.end(), txn);
    if (it != active_txns_.end()) {
        active_txns_.erase(it);
    }
    delete txn;
    return true;
}

Transaction* TransactionManager::GetCurrentTransaction() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_txns_.empty()) return nullptr;
    return active_txns_.back();
}

}  // namespace goods_db
