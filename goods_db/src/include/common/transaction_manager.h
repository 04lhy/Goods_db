#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "common/rid.h"

namespace goods_db {

// Forward declarations
class DiskManager;

/**
 * Savepoint — a named point within a transaction that can be rolled back to.
 */
struct Savepoint {
    std::string name;
    uint64_t txn_id;
    int64_t created_at;  // monotonic counter within the transaction
};

/**
 * Transaction — represents a database transaction with isolation level support.
 */
struct Transaction {
    enum class State : uint8_t { ACTIVE, COMMITTED, ABORTED };

    enum class IsolationLevel : uint8_t {
        READ_UNCOMMITTED = 0,
        READ_COMMITTED = 1,
        REPEATABLE_READ = 2,
        SERIALIZABLE = 3,
    };

    uint64_t txn_id;
    State state{State::ACTIVE};
    int64_t start_ts{0};  // start timestamp for MVCC / ordering
    IsolationLevel isolation_level{IsolationLevel::READ_COMMITTED};

    // Savepoints within this transaction (ordered by creation)
    std::vector<Savepoint> savepoints;

    Transaction() : txn_id(0), start_ts(0) {}
    explicit Transaction(uint64_t id) : txn_id(id), start_ts(0) {}

    /** Convert isolation level to string */
    static const char* IsolationLevelToString(IsolationLevel level) {
        switch (level) {
            case IsolationLevel::READ_UNCOMMITTED: return "READ UNCOMMITTED";
            case IsolationLevel::READ_COMMITTED:   return "READ COMMITTED";
            case IsolationLevel::REPEATABLE_READ:  return "REPEATABLE READ";
            case IsolationLevel::SERIALIZABLE:     return "SERIALIZABLE";
            default:                                return "UNKNOWN";
        }
    }

    /** Parse isolation level from string */
    static IsolationLevel IsolationLevelFromString(const std::string& s) {
        if (s == "READ UNCOMMITTED" || s == "READ-UNCOMMITTED") return IsolationLevel::READ_UNCOMMITTED;
        if (s == "READ COMMITTED" || s == "READ-COMMITTED") return IsolationLevel::READ_COMMITTED;
        if (s == "REPEATABLE READ" || s == "REPEATABLE-READ") return IsolationLevel::REPEATABLE_READ;
        if (s == "SERIALIZABLE") return IsolationLevel::SERIALIZABLE;
        return IsolationLevel::READ_COMMITTED;  // default
    }
};

/**
 * TransactionManager — manages transaction lifecycle with:
 *   - Begin / Commit / Rollback
 *   - Savepoint support (SAVEPOINT / RELEASE SAVEPOINT / ROLLBACK TO SAVEPOINT)
 *   - Isolation level configuration
 *   - XID persistence (survives server restart)
 *   - Transaction-local change tracking for undo
 *   - Statistics for monitoring
 */
class TransactionManager {
public:
    TransactionManager();
    ~TransactionManager();

    /** Initialize with optional XID persistence file */
    void Initialize(const std::string& xid_file = "");

    /** Begin a new transaction with the default isolation level */
    Transaction* Begin();

    /**
     * Begin a transaction with a specific isolation level.
     * @param level The isolation level
     * @return New transaction object
     */
    Transaction* Begin(Transaction::IsolationLevel level);

    /** Commit a transaction (release all locks, make changes permanent) */
    bool Commit(Transaction* txn);

    /** Rollback (abort) a transaction (release all locks, undo changes) */
    bool Rollback(Transaction* txn);

    // ---- Savepoint operations ------------------------------------------------

    /**
     * Create a savepoint within the current transaction.
     * @param name Unique savepoint name within the transaction
     * @return true on success
     */
    bool CreateSavepoint(const std::string& name);

    /**
     * Release a savepoint (removes it without affecting transaction state).
     * @param name Savepoint name
     * @return true if the savepoint existed
     */
    bool ReleaseSavepoint(const std::string& name);

    /**
     * Rollback to a savepoint (undoes work after the savepoint).
     * @param name Savepoint name
     * @return true if the savepoint existed
     */
    bool RollbackToSavepoint(const std::string& name);

    // ---- Transaction access --------------------------------------------------

    /** Get the current active transaction (thread-local) */
    Transaction* GetCurrentTransaction();

    /** Set the current thread-local transaction */
    void SetCurrentTransaction(Transaction* txn);

    /** Get the number of active transactions */
    size_t GetActiveTransactionCount() const;

    // ---- Isolation level -----------------------------------------------------

    /** Set the default isolation level for new transactions */
    void SetDefaultIsolationLevel(Transaction::IsolationLevel level);

    /** Get the default isolation level */
    Transaction::IsolationLevel GetDefaultIsolationLevel() const {
        return default_isolation_level_;
    }

    // ---- XID management ------------------------------------------------------

    /** Allocate a new transaction ID (monotonically increasing) */
    uint64_t AllocateXid();

    /** Get the last allocated XID */
    uint64_t GetLastXid() const { return last_xid_.load(); }

    /** Persist the current XID counter to disk */
    bool PersistXid();

    /** Load the XID counter from disk */
    bool LoadXid(const std::string& xid_file);

    // ---- Statistics ----------------------------------------------------------

    /** Get the total number of transactions started */
    uint64_t GetTotalTxnsStarted() const { return total_txns_started_.load(); }

    /** Get the total number of transactions committed */
    uint64_t GetTotalTxnsCommitted() const { return total_txns_committed_.load(); }

    /** Get the total number of transactions rolled back */
    uint64_t GetTotalTxnsRolledBack() const { return total_txns_rolled_back_.load(); }

private:
    mutable std::mutex mutex_;
    std::vector<Transaction*> active_txns_;

    // Thread-local current transaction
    static thread_local Transaction* current_txn_;

    // XID allocator
    std::atomic<uint64_t> last_xid_{0};
    std::string xid_file_;

    // Default isolation level
    Transaction::IsolationLevel default_isolation_level_{
        Transaction::IsolationLevel::READ_COMMITTED};

    // Statistics
    std::atomic<uint64_t> total_txns_started_{0};
    std::atomic<uint64_t> total_txns_committed_{0};
    std::atomic<uint64_t> total_txns_rolled_back_{0};

    // ---- Internal helpers ----------------------------------------------------

    /** Write XID to persistence file */
    bool WriteXidFile(uint64_t xid);
};

}  // namespace goods_db
