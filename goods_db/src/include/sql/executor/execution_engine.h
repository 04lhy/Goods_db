#pragma once

#include <memory>
#include <string>
#include <vector>

#include "sql/executor/abstract_executor.h"
#include "storage/table/tuple.h"
#include "type/schema.h"

namespace goods_db {

// Forward declarations
class Catalog;
class BufferPoolManager;
class DiskManager;
class PlanNode;
class AuthManager;
class LogManager;
class LockManager;
class TransactionManager;

/**
 * ExecutionEngine — unified entry point for SQL execution.
 *
 * Provides the complete pipeline:
 *   SQL text → Parse → Bind → Plan → Optimize → Execute
 *
 * Also handles administrative SQL commands that bypass the parser:
 *   - SHOW DATABASES / SHOW TABLES / SHOW COLUMNS / DESCRIBE
 *   - CREATE DATABASE / DROP DATABASE
 *   - Security: CREATE USER / DROP USER / ALTER USER / SET PASSWORD
 *               GRANT / REVOKE / FLUSH PRIVILEGES
 *   - Transaction: START TRANSACTION / BEGIN / COMMIT / ROLLBACK
 *                  SAVEPOINT / RELEASE SAVEPOINT / ROLLBACK TO SAVEPOINT
 *                  SET TRANSACTION ISOLATION LEVEL
 *   - Log management: SHOW LOGS / SHOW BINLOG EVENTS / SHOW MASTER STATUS
 *                     FLUSH LOGS / PURGE BINARY LOGS / RESET MASTER
 *
 * Usage:
 *   ExecutionEngine engine(bpm, dm, catalog);
 *   engine.SetAuthManager(&auth_mgr);
 *   engine.SetLogManager(&log_mgr);
 *   engine.SetLockManager(&lock_mgr);
 *   engine.SetTransactionManager(&txn_mgr);
 *   std::vector<Tuple> results;
 *   const Schema* output_schema;
 *   engine.ExecuteSQL("SELECT * FROM t WHERE id = 1", &results, &output_schema);
 */
class ExecutionEngine {
public:
    ExecutionEngine(BufferPoolManager* bpm, DiskManager* dm, Catalog* catalog);

    // ---- Manager setters (optional, for administrative commands) ---------------
    void SetAuthManager(AuthManager* auth_mgr) { auth_mgr_ = auth_mgr; }
    void SetLogManager(LogManager* log_mgr) { log_mgr_ = log_mgr; }
    void SetLockManager(LockManager* lock_mgr) { lock_mgr_ = lock_mgr; }
    void SetTransactionManager(TransactionManager* txn_mgr) { txn_mgr_ = txn_mgr; }

    /**
     * Execute a SQL string end-to-end.
     * @param sql The SQL text to execute
     * @param results [out] Result tuples (for SELECT queries)
     * @param output_schema [out] Schema of result tuples
     * @return 0 on success, -1 on error
     */
    int ExecuteSQL(const std::string& sql, std::vector<Tuple>* results,
                   const Schema** output_schema);

    /**
     * Execute a pre-built plan tree.
     */
    int ExecutePlan(PlanNode* plan, std::vector<Tuple>* results,
                    const Schema** output_schema);

    /** Get the last error message */
    const std::string& GetLastError() const { return last_error_; }

    /** Get the number of rows affected by the last DML statement */
    int GetAffectedRows() const { return affected_rows_; }

    /** Get the optimizer for standalone use */
    class Optimizer* GetOptimizer() { return optimizer_.get(); }

    /** Check if the last command was a transaction control statement */
    bool IsTxnControl() const { return is_txn_control_; }

private:
    BufferPoolManager* bpm_;
    DiskManager* dm_;
    Catalog* catalog_;
    std::string last_error_;
    int affected_rows_{0};
    bool is_txn_control_{false};
    std::unique_ptr<class Optimizer> optimizer_;
    std::unique_ptr<PlanNode> last_plan_;  // keep plan alive for output_schema pointer

    // Optional manager references for administrative commands
    AuthManager* auth_mgr_{nullptr};
    LogManager* log_mgr_{nullptr};
    LockManager* lock_mgr_{nullptr};
    TransactionManager* txn_mgr_{nullptr};

    // ---- SHOW command handlers (bypass the SQL parser) ------------------------
    int ExecuteShowDatabases(std::vector<Tuple>* results,
                             const Schema** output_schema);
    int ExecuteShowTables(const std::string& sql,
                          std::vector<Tuple>* results,
                          const Schema** output_schema);
    int ExecuteShowColumns(const std::string& sql,
                           std::vector<Tuple>* results,
                           const Schema** output_schema);

    // ---- DDL command handlers (bypass the SQL parser) -------------------------
    int ExecuteCreateDatabase(const std::string& sql,
                               std::vector<Tuple>* results,
                               const Schema** output_schema);
    int ExecuteDropDatabase(const std::string& sql,
                             std::vector<Tuple>* results,
                             const Schema** output_schema);

    // ---- Security command handlers (bypass the SQL parser) --------------------
    int ExecuteCreateUser(const std::string& sql,
                          std::vector<Tuple>* results,
                          const Schema** output_schema);
    int ExecuteDropUser(const std::string& sql,
                        std::vector<Tuple>* results,
                        const Schema** output_schema);
    int ExecuteAlterUser(const std::string& sql,
                         std::vector<Tuple>* results,
                         const Schema** output_schema);
    int ExecuteSetPassword(const std::string& sql,
                           std::vector<Tuple>* results,
                           const Schema** output_schema);
    int ExecuteGrant(const std::string& sql,
                     std::vector<Tuple>* results,
                     const Schema** output_schema);
    int ExecuteRevoke(const std::string& sql,
                      std::vector<Tuple>* results,
                      const Schema** output_schema);
    int ExecuteFlushPrivileges(std::vector<Tuple>* results,
                               const Schema** output_schema);

    // ---- Transaction command handlers (bypass the SQL parser) -----------------
    int ExecuteStartTransaction(std::vector<Tuple>* results,
                                const Schema** output_schema);
    int ExecuteCommit(std::vector<Tuple>* results,
                      const Schema** output_schema);
    int ExecuteRollback(std::vector<Tuple>* results,
                        const Schema** output_schema);
    int ExecuteSavepoint(const std::string& sql,
                         std::vector<Tuple>* results,
                         const Schema** output_schema);
    int ExecuteReleaseSavepoint(const std::string& sql,
                                std::vector<Tuple>* results,
                                const Schema** output_schema);
    int ExecuteRollbackToSavepoint(const std::string& sql,
                                   std::vector<Tuple>* results,
                                   const Schema** output_schema);
    int ExecuteSetTransaction(const std::string& sql,
                              std::vector<Tuple>* results,
                              const Schema** output_schema);

    // ---- Log management command handlers (bypass the SQL parser) --------------
    int ExecuteShowLogs(std::vector<Tuple>* results,
                        const Schema** output_schema);
    int ExecuteShowBinlogEvents(const std::string& sql,
                                std::vector<Tuple>* results,
                                const Schema** output_schema);
    int ExecuteShowMasterStatus(std::vector<Tuple>* results,
                                const Schema** output_schema);
    int ExecuteFlushLogs(std::vector<Tuple>* results,
                         const Schema** output_schema);
    int ExecutePurgeBinaryLogs(const std::string& sql,
                               std::vector<Tuple>* results,
                               const Schema** output_schema);
    int ExecuteResetMaster(std::vector<Tuple>* results,
                           const Schema** output_schema);

    // ---- Helper: parse privilege names from a comma-separated list ------------
    static uint32_t ParsePrivilegeList(const std::string& priv_list);
};

}  // namespace goods_db
