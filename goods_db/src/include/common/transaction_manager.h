#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "common/rid.h"

namespace goods_db {

/**
 * Transaction — represents a database transaction.
 */
struct Transaction {
    enum class State { ACTIVE, COMMITTED, ABORTED };

    uint64_t txn_id;
    State state{State::ACTIVE};
    int64_t start_ts;  // start timestamp for MVCC

    Transaction() : txn_id(NextTxnId()), start_ts(0) {}

    static uint64_t NextTxnId() {
        static std::atomic<uint64_t> counter{1};
        return counter.fetch_add(1);
    }
};

/**
 * TransactionManager — manages transaction lifecycle.
 *
 * Provides:
 *   - Begin / Commit / Rollback
 *   - Transaction-local change tracking
 *   - Write-ahead logging preparation
 */
class TransactionManager {
public:
    TransactionManager() = default;
    ~TransactionManager() = default;

    /** Begin a new transaction */
    Transaction* Begin();

    /** Commit a transaction */
    bool Commit(Transaction* txn);

    /** Rollback (abort) a transaction */
    bool Rollback(Transaction* txn);

    /** Get the current active transaction (thread-local) */
    Transaction* GetCurrentTransaction();

private:
    std::mutex mutex_;
    std::vector<Transaction*> active_txns_;
};

}  // namespace goods_db
