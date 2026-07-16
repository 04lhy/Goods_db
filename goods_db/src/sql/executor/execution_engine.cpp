#include "sql/executor/execution_engine.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

#include "catalog/catalog.h"
#include "common/lock_manager.h"
#include "common/transaction_manager.h"
#include "sql/binder/binder.h"
#include "sql/executor/abstract_executor.h"
#include "sql/goods_handler.h"
#include "sql/handler.h"
#include "sql/log/log_manager.h"
#include "sql/network/connection.h"
#include "sql/optimizer/optimizer.h"
#include "sql/parser/ast.h"
#include "sql/parser/parser.h"
#include "sql/planner/planner.h"
#include "sql/security/auth_manager.h"
#include "sql/server/connection_handler.h"

namespace goods_db {

// =============================================================================
// Helper: trim whitespace, semicolons, and backtick quotes from identifiers
// =============================================================================
static void TrimIdentifier(std::string& s) {
    // Trim trailing whitespace, semicolons, backticks
    while (!s.empty() && (s.back() == ';' || s.back() == '`' || isspace(s.back())))
        s.pop_back();
    // Trim leading whitespace, backticks
    while (!s.empty() && (s.front() == '`' || isspace(s.front())))
        s.erase(0, 1);
}

ExecutionEngine::ExecutionEngine(BufferPoolManager* bpm, DiskManager* dm,
                                   Catalog* catalog)
    : bpm_(bpm), dm_(dm), catalog_(catalog) {
    optimizer_ = std::make_unique<Optimizer>();
}

int ExecutionEngine::ExecuteSQL(const std::string& sql,
                                  std::vector<Tuple>* results,
                                  const Schema** output_schema) {
    last_error_.clear();
    query_counter_++;

    // ---- Pre-process: handle SHOW commands (not supported by the SQL parser) ----
    std::string upper_sql = sql;
    for (auto& c : upper_sql) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    if (upper_sql.rfind("SHUTDOWN", 0) == 0) {
        if (!CheckAdminAccess()) return -1;
        return ExecuteShutdown(results, output_schema);
    }
    if (upper_sql.rfind("SHOW STATUS", 0) == 0) {
        return ExecuteShowStatus(results, output_schema);
    }
    if (upper_sql.rfind("SHOW PROCESSLIST", 0) == 0) {
        return ExecuteShowProcesslist(results, output_schema);
    }
    if (upper_sql.rfind("SHOW USERS", 0) == 0) {
        return ExecuteShowUsers(results, output_schema);
    }
    if (upper_sql.rfind("SHOW DATABASES", 0) == 0 || upper_sql.rfind("SHOW SCHEMAS", 0) == 0) {
        return ExecuteShowDatabases(results, output_schema);
    }
    if (upper_sql.rfind("SHOW TABLES", 0) == 0) {
        return ExecuteShowTables(sql, results, output_schema);
    }
    if (upper_sql.rfind("SHOW COLUMNS FROM", 0) == 0 ||
        upper_sql.rfind("SHOW FIELDS FROM", 0) == 0 ||
        upper_sql.rfind("DESCRIBE ", 0) == 0 ||
        upper_sql.rfind("DESC ", 0) == 0) {
        return ExecuteShowColumns(sql, results, output_schema);
    }

    // Handle CREATE DATABASE / DROP DATABASE (bypass parser, as libpg_query may not support them)
    if (upper_sql.rfind("CREATE DATABASE ", 0) == 0 ||
        upper_sql.rfind("CREATE SCHEMA ", 0) == 0) {
        if (!CheckPrivilegeForStatement(Privilege::CREATE, "*")) return -1;
        return ExecuteCreateDatabase(sql, results, output_schema);
    }
    if (upper_sql.rfind("DROP DATABASE ", 0) == 0 ||
        upper_sql.rfind("DROP SCHEMA ", 0) == 0) {
        if (!CheckPrivilegeForStatement(Privilege::DROP, "*")) return -1;
        return ExecuteDropDatabase(sql, results, output_schema);
    }

    // ---- Security commands ----------------------------------------------------
    if (upper_sql.rfind("CREATE USER ", 0) == 0 ||
        upper_sql.rfind("DROP USER ", 0) == 0 ||
        upper_sql.rfind("ALTER USER ", 0) == 0 ||
        upper_sql.rfind("SET PASSWORD", 0) == 0 ||
        upper_sql.rfind("GRANT ", 0) == 0 ||
        upper_sql.rfind("REVOKE ", 0) == 0 ||
        upper_sql == "RELOAD" ||
        upper_sql.rfind("FLUSH HOSTS", 0) == 0 ||
        upper_sql.rfind("FLUSH PRIVILEGES", 0) == 0) {
        if (!CheckAdminAccess()) return -1;
    }
    if (upper_sql.rfind("CREATE USER ", 0) == 0) {
        return ExecuteCreateUser(sql, results, output_schema);
    }
    if (upper_sql.rfind("DROP USER ", 0) == 0) {
        return ExecuteDropUser(sql, results, output_schema);
    }
    if (upper_sql.rfind("ALTER USER ", 0) == 0) {
        return ExecuteAlterUser(sql, results, output_schema);
    }
    if (upper_sql.rfind("SET PASSWORD", 0) == 0) {
        return ExecuteSetPassword(sql, results, output_schema);
    }
    if (upper_sql.rfind("GRANT ", 0) == 0) {
        return ExecuteGrant(sql, results, output_schema);
    }
    if (upper_sql.rfind("REVOKE ", 0) == 0) {
        return ExecuteRevoke(sql, results, output_schema);
    }
    if (upper_sql == "RELOAD") {
        return ExecuteFlushPrivileges(results, output_schema);
    }
    if (upper_sql.rfind("FLUSH HOSTS", 0) == 0) {
        return ExecuteFlushHosts(results, output_schema);
    }
    if (upper_sql.rfind("FLUSH PRIVILEGES", 0) == 0) {
        return ExecuteFlushPrivileges(results, output_schema);
    }
    if (upper_sql.rfind("KILL ", 0) == 0) {
        if (!CheckAdminAccess()) return -1;
    }
    if (upper_sql.rfind("KILL ", 0) == 0) {
        return ExecuteKill(sql, results, output_schema);
    }

    // ---- Transaction control commands -----------------------------------------
    is_txn_control_ = false;
    if (upper_sql == "START TRANSACTION" || upper_sql == "BEGIN" ||
        upper_sql.rfind("START TRANSACTION ", 0) == 0) {
        is_txn_control_ = true;
        return ExecuteStartTransaction(results, output_schema);
    }
    if (upper_sql == "COMMIT" || upper_sql.rfind("COMMIT", 0) == 0) {
        is_txn_control_ = true;
        return ExecuteCommit(results, output_schema);
    }
    if (upper_sql == "ROLLBACK" ||
        upper_sql.rfind("ROLLBACK TO SAVEPOINT ", 0) == 0) {
        is_txn_control_ = true;
        if (upper_sql.rfind("ROLLBACK TO SAVEPOINT ", 0) == 0) {
            return ExecuteRollbackToSavepoint(sql, results, output_schema);
        }
        return ExecuteRollback(results, output_schema);
    }
    if (upper_sql.rfind("SAVEPOINT ", 0) == 0) {
        is_txn_control_ = true;
        return ExecuteSavepoint(sql, results, output_schema);
    }
    if (upper_sql.rfind("RELEASE SAVEPOINT ", 0) == 0) {
        is_txn_control_ = true;
        return ExecuteReleaseSavepoint(sql, results, output_schema);
    }
    if (upper_sql.rfind("SET TRANSACTION", 0) == 0 ||
        upper_sql.rfind("SET SESSION TRANSACTION", 0) == 0) {
        is_txn_control_ = true;
        return ExecuteSetTransaction(sql, results, output_schema);
    }

    // ---- Log management commands ----------------------------------------------
    if (upper_sql.rfind("SHOW GRANTS", 0) == 0) {
        return ExecuteShowGrants(sql, results, output_schema);
    }
    if (upper_sql.rfind("SHOW ERRORLOG", 0) == 0) {
        return ExecuteShowErrorLog(sql, results, output_schema);
    }
    if (upper_sql.rfind("SHOW QUERYLOG", 0) == 0) {
        return ExecuteShowQueryLog(sql, results, output_schema);
    }
    if (upper_sql.rfind("SHOW BINARYLOG", 0) == 0) {
        return ExecuteShowBinaryLog(sql, results, output_schema);
    }
    if (upper_sql.rfind("SHOW LOGS", 0) == 0) {
        return ExecuteShowLogs(results, output_schema);
    }
    if (upper_sql.rfind("SHOW BINLOG EVENTS", 0) == 0) {
        return ExecuteShowBinlogEvents(sql, results, output_schema);
    }
    if (upper_sql.rfind("SHOW MASTER STATUS", 0) == 0) {
        return ExecuteShowMasterStatus(results, output_schema);
    }
    if (upper_sql.rfind("FLUSH TABLES", 0) == 0) {
        return ExecuteFlushTables(results, output_schema);
    }
    if (upper_sql.rfind("FLUSH LOGS", 0) == 0) {
        return ExecuteFlushLogs(results, output_schema);
    }
    if (upper_sql.rfind("PURGE BINARY LOGS ", 0) == 0) {
        return ExecutePurgeBinaryLogs(sql, results, output_schema);
    }
    if (upper_sql.rfind("RESET MASTER", 0) == 0) {
        return ExecuteResetMaster(results, output_schema);
    }
    if (upper_sql.rfind("REGISTER_FK ", 0) == 0) {
        return ExecuteRegisterFk(sql, results, output_schema);
    }

    // Step 1: Parse
    Parser parser;
    std::vector<std::unique_ptr<ASTStatement>> ast_stmts;
    try {
        ast_stmts = parser.Parse(sql);
    } catch (const std::exception& e) {
        last_error_ = std::string("Parse error: ") + e.what();
        return -1;
    }
    if (ast_stmts.empty()) {
        last_error_ = "No statements parsed";
        return -1;
    }

    // ---- Permission check: DML / DDL statements ---------------------------
    if (auth_mgr_ && !current_user_.empty()) {
        std::string table_name = ExtractTableName(ast_stmts[0].get());
        Privilege required = GetRequiredPrivilege(
            static_cast<uint8_t>(ast_stmts[0]->GetType()));
        if (!CheckPrivilegeForStatement(required, table_name)) {
            return -1;
        }
    }

    // Step 2: Bind
    Binder binder(catalog_);
    auto bound = binder.Bind(ast_stmts[0].get());
    if (!bound) {
        last_error_ = binder.error_message();
        if (last_error_.empty()) last_error_ = "Binding failed";
        return -1;
    }

    // Step 3: Plan
    Planner planner;
    auto plan = planner.Plan(bound.get());
    if (!plan) {
        last_error_ = "Planning failed";
        return -1;
    }

    // Step 4: Optimize
    plan = optimizer_->Optimize(std::move(plan));
    if (!plan) {
        last_error_ = "Optimization failed";
        return -1;
    }

    // Step 5: Execute
    int ret = ExecutePlan(plan.get(), results, output_schema);

    // Keep the plan alive — output_schema may point into it
    last_plan_ = std::move(plan);

    return ret;
}

int ExecutionEngine::ExecutePlan(PlanNode* plan,
                                   std::vector<Tuple>* results,
                                   const Schema** output_schema) {
    if (!plan) return -1;

    // Create handler context
    goods_handler handler(bpm_, dm_, catalog_);

    ExecutorContext ctx;
    ctx.table_handler = &handler;
    ctx.catalog = catalog_;
    ctx.bpm = bpm_;
    ctx.disk_manager = dm_;
    ctx.affected_rows = 0;

    // Create executor tree
    auto executor = ExecutorFactory::Create(&ctx, plan);
    if (!executor) {
        last_error_ = "Executor creation failed";
        return -1;
    }

    // Initialize and execute
    executor->Init();

    if (output_schema) {
        *output_schema = executor->GetOutputSchema();
    }

    if (results) {
        Tuple tuple;
        while (executor->Next(&tuple, nullptr)) {
            results->push_back(std::move(tuple));
        }
    } else {
        // DML: execute once
        Tuple dummy;
        executor->Next(&dummy, nullptr);
    }

    // Capture affected rows from DML executors
    affected_rows_ = ctx.affected_rows;

    return 0;
}

// =============================================================================
// SHOW command implementations (catalog introspection, bypass parser)
// =============================================================================

int ExecutionEngine::ExecuteShowDatabases(std::vector<Tuple>* results,
                                           const Schema** output_schema) {
    // Return all databases from the catalog (or the default "goods_db")
    static Schema show_db_schema({Column("Database", TypeId::VARCHAR, 256)});

    if (results) {
        if (catalog_ && !catalog_->ListDatabases().empty()) {
            for (const auto& db_name : catalog_->ListDatabases()) {
                std::vector<Value> row;
                row.push_back(Value::CreateVarchar(db_name));
                results->push_back(Tuple::CreateFromValues(row, &show_db_schema));
            }
        } else {
            // Fallback: return the default database name
            std::vector<Value> row;
            row.push_back(Value::CreateVarchar("goods_db"));
            results->push_back(Tuple::CreateFromValues(row, &show_db_schema));
        }
    }
    if (output_schema) *output_schema = &show_db_schema;
    return 0;
}

int ExecutionEngine::ExecuteShowTables(const std::string& sql,
                                         std::vector<Tuple>* results,
                                         const Schema** output_schema) {
    if (!catalog_) {
        last_error_ = "No catalog available";
        return -1;
    }

    // Parse optional FROM <db> clause
    std::string db_filter;
    std::string upper = sql;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    size_t from_pos = upper.find(" FROM ");
    if (from_pos != std::string::npos) {
        db_filter = sql.substr(from_pos + 6);  // " FROM " = 6 chars
        TrimIdentifier(db_filter);
    }

    auto all_tables = catalog_->GetAllTables();

    // Use static schema — column name is "Tables_in_goods_db" (or "Tables_in_default"
    // for backward compatibility). The column name being dynamic per-db is cosmetic.
    static Schema show_tbl_schema({Column("Tables_in_goods_db", TypeId::VARCHAR, 256)});

    if (results) {
        for (auto* t : all_tables) {
            // If a database filter is specified, only return tables from that database
            if (!db_filter.empty() && db_filter != "goods_db" && db_filter != "default") {
                continue;
            }
            std::vector<Value> row;
            row.push_back(Value::CreateVarchar(t->table_name));
            results->push_back(Tuple::CreateFromValues(row, &show_tbl_schema));
        }
    }
    if (output_schema) *output_schema = &show_tbl_schema;
    return 0;
}

int ExecutionEngine::ExecuteShowColumns(const std::string& sql,
                                         std::vector<Tuple>* results,
                                         const Schema** output_schema) {
    if (!catalog_) {
        last_error_ = "No catalog available";
        return -1;
    }

    // Parse: SHOW COLUMNS FROM <table> / SHOW COLUMNS FROM <db>.<table> / DESCRIBE <table>
    std::string full_name;
    std::string upper = sql;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    // Match patterns with precise lengths to avoid off-by-one errors
    if (upper.rfind("SHOW COLUMNS FROM ", 0) == 0) {
        full_name = sql.substr(18);  // "SHOW COLUMNS FROM " = 18 chars
    } else if (upper.rfind("SHOW FIELDS FROM ", 0) == 0) {
        full_name = sql.substr(17);  // "SHOW FIELDS FROM " = 17 chars
    } else if (upper.rfind("DESCRIBE ", 0) == 0) {
        full_name = sql.substr(9);   // "DESCRIBE " = 9 chars
    } else if (upper.rfind("DESC ", 0) == 0) {
        full_name = sql.substr(5);   // "DESC " = 5 chars
    }

    if (full_name.empty()) {
        last_error_ = "Invalid SHOW COLUMNS / DESCRIBE syntax";
        return -1;
    }

    // Clean up the identifier: trim whitespace, semicolons, and backticks
    TrimIdentifier(full_name);

    // Parse "db.table" or just "table"
    std::string table_name = full_name;
    size_t dot = full_name.find('.');
    if (dot != std::string::npos) {
        table_name = full_name.substr(dot + 1);
        // Also trim backticks from the table portion
        TrimIdentifier(table_name);
    }

    TableInfo* info = catalog_->GetTable(table_name);
    if (!info) {
        last_error_ = "Table not found: " + table_name;
        return -1;
    }

    static Schema show_col_schema({
        Column("Field", TypeId::VARCHAR, 256),
        Column("Type", TypeId::VARCHAR, 64),
        Column("Null", TypeId::VARCHAR, 4),
        Column("Key", TypeId::VARCHAR, 4),
        Column("Default", TypeId::VARCHAR, 256),
        Column("Extra", TypeId::VARCHAR, 256),
    });

    if (results) {
        for (uint32_t i = 0; i < info->schema.GetColumnCount(); i++) {
            auto& col = info->schema.GetColumn(i);
            std::vector<Value> row;
            row.push_back(Value::CreateVarchar(col.column_name));
            std::string type_str;
            switch (col.column_type) {
                case TypeId::BOOLEAN: type_str = "boolean"; break;
                case TypeId::TINYINT: type_str = "tinyint"; break;
                case TypeId::SMALLINT: type_str = "smallint"; break;
                case TypeId::INTEGER: type_str = "int"; break;
                case TypeId::BIGINT: type_str = "bigint"; break;
                case TypeId::DECIMAL: type_str = "decimal"; break;
                case TypeId::VARCHAR: type_str = "varchar(" + std::to_string(col.max_length ? col.max_length : 255) + ")"; break;
                case TypeId::TIMESTAMP: type_str = "timestamp"; break;
                default: type_str = "unknown"; break;
            }
            row.push_back(Value::CreateVarchar(type_str));
            row.push_back(Value::CreateVarchar(col.is_nullable ? "YES" : "NO"));
            row.push_back(Value::CreateVarchar(col.is_primary_key ? "PRI" : ""));
            row.push_back(Value::CreateVarchar(col.is_nullable ? "NULL" : ""));
            row.push_back(Value::CreateVarchar(""));
            results->push_back(Tuple::CreateFromValues(row, &show_col_schema));
        }
    }
    if (output_schema) *output_schema = &show_col_schema;
    return 0;
}

// =============================================================================
// CREATE DATABASE / DROP DATABASE support
// =============================================================================

int ExecutionEngine::ExecuteCreateDatabase(const std::string& sql,
                                            std::vector<Tuple>* /*results*/,
                                            const Schema** /*output_schema*/) {
    std::string upper = sql;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    // Extract database name: "CREATE DATABASE <name>" or "CREATE SCHEMA <name>"
    std::string marker = (upper.rfind("CREATE DATABASE ", 0) == 0) ? "CREATE DATABASE " : "CREATE SCHEMA ";
    std::string db_name = sql.substr(marker.size());

    // Handle IF NOT EXISTS
    bool if_not_exists = false;
    std::string upper_db = db_name;
    for (auto& c : upper_db) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    if (upper_db.rfind("IF NOT EXISTS ", 0) == 0) {
        if_not_exists = true;
        db_name = db_name.substr(15);  // "IF NOT EXISTS " = 15 chars
    }

    TrimIdentifier(db_name);

    if (db_name.empty()) {
        last_error_ = "Missing database name in CREATE DATABASE";
        return -1;
    }

    if (!catalog_) {
        last_error_ = "No catalog available";
        return -1;
    }

    bool ok = catalog_->CreateDatabase(db_name, if_not_exists);
    if (!ok) {
        last_error_ = if_not_exists ? "" : "Database already exists: " + db_name;
        // IF NOT EXISTS is not an error
        if (if_not_exists) return 0;
        return -1;
    }

    return 0;
}

int ExecutionEngine::ExecuteDropDatabase(const std::string& sql,
                                          std::vector<Tuple>* /*results*/,
                                          const Schema** /*output_schema*/) {
    std::string upper = sql;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    // Extract database name: "DROP DATABASE <name>" or "DROP SCHEMA <name>"
    std::string marker = (upper.rfind("DROP DATABASE ", 0) == 0) ? "DROP DATABASE " : "DROP SCHEMA ";
    std::string db_name = sql.substr(marker.size());

    // Handle IF EXISTS
    bool if_exists = false;
    std::string upper_db = db_name;
    for (auto& c : upper_db) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    if (upper_db.rfind("IF EXISTS ", 0) == 0) {
        if_exists = true;
        db_name = db_name.substr(10);  // "IF EXISTS " = 10 chars
    }

    TrimIdentifier(db_name);

    if (db_name.empty()) {
        last_error_ = "Missing database name in DROP DATABASE";
        return -1;
    }

    if (!catalog_) {
        last_error_ = "No catalog available";
        return -1;
    }

    bool ok = catalog_->DropDatabase(db_name, if_exists);
    if (!ok) {
        last_error_ = if_exists ? "" : "Database not found: " + db_name;
        if (if_exists) return 0;
        return -1;
    }

    return 0;
}

// =============================================================================
// Security command implementations
// =============================================================================

// Helper: parse 'user'@'host' from SQL text
static bool ParseUserHost(const std::string& s, size_t start,
                          std::string& user, std::string& host) {
    // Expected format: 'user'@'host' or "user"@"host"
    while (start < s.size() && isspace(static_cast<unsigned char>(s[start]))) start++;
    if (start >= s.size()) return false;

    char quote = s[start];
    if (quote != '\'' && quote != '"') return false;
    start++;  // skip opening quote

    size_t end = s.find(quote, start);
    if (end == std::string::npos) return false;
    user = s.substr(start, end - start);
    end++;  // skip closing quote

    // Expect '@'
    while (end < s.size() && isspace(static_cast<unsigned char>(s[end]))) end++;
    if (end >= s.size() || s[end] != '@') return false;
    end++;  // skip '@'

    while (end < s.size() && isspace(static_cast<unsigned char>(s[end]))) end++;
    if (end >= s.size()) return false;
    quote = s[end];
    if (quote != '\'' && quote != '"') return false;
    end++;  // skip opening quote

    size_t host_end = s.find(quote, end);
    if (host_end == std::string::npos) return false;
    host = s.substr(end, host_end - end);
    return true;
}

// Helper: parse a single-quoted string from SQL text at position
static std::string ParseQuotedString(const std::string& s, size_t& pos) {
    while (pos < s.size() && isspace(static_cast<unsigned char>(s[pos]))) pos++;
    if (pos >= s.size()) return "";

    char quote = s[pos];
    if (quote != '\'' && quote != '"') {
        // Unquoted identifier (read until space/semicolon)
        size_t start = pos;
        while (pos < s.size() && !isspace(static_cast<unsigned char>(s[pos])) && s[pos] != ';')
            pos++;
        return s.substr(start, pos - start);
    }
    pos++;  // skip opening quote
    size_t start = pos;
    while (pos < s.size() && s[pos] != quote) {
        if (s[pos] == '\\' && pos + 1 < s.size()) pos++;  // escape
        pos++;
    }
    std::string result = s.substr(start, pos - start);
    if (pos < s.size()) pos++;  // skip closing quote
    return result;
}

int ExecutionEngine::ExecuteCreateUser(const std::string& sql,
                                        std::vector<Tuple>* /*results*/,
                                        const Schema** /*output_schema*/) {
    if (!auth_mgr_) {
        last_error_ = "AuthManager not available";
        return -1;
    }

    // Syntax: CREATE USER 'user'@'host' IDENTIFIED BY 'password'
    // or:     CREATE USER 'user'@'host'
    std::string upper = sql;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    // Find "CREATE USER " prefix
    size_t pos = 12;  // strlen("CREATE USER ")
    std::string user, host, password;

    // Parse user@host
    if (!ParseUserHost(sql, pos, user, host)) {
        last_error_ = "Invalid CREATE USER syntax. Expected: CREATE USER 'user'@'host' [IDENTIFIED BY 'password']";
        return -1;
    }

    // Check for IDENTIFIED BY
    size_t ident_pos = upper.find("IDENTIFIED BY", pos + (user.size() + host.size() + 6));
    if (ident_pos != std::string::npos) {
        size_t pwd_start = ident_pos + 13;  // strlen("IDENTIFIED BY")
        while (pwd_start < sql.size() && isspace(static_cast<unsigned char>(sql[pwd_start]))) pwd_start++;
        password = ParseQuotedString(sql, pwd_start);
    }

    if (!auth_mgr_->CreateUser(user, host, password)) {
        last_error_ = "Failed to create user '" + user + "'@'" + host + "' (may already exist)";
        return -1;
    }

    return 0;
}

int ExecutionEngine::ExecuteDropUser(const std::string& sql,
                                      std::vector<Tuple>* /*results*/,
                                      const Schema** /*output_schema*/) {
    if (!auth_mgr_) {
        last_error_ = "AuthManager not available";
        return -1;
    }

    // Syntax: DROP USER 'user'@'host'
    size_t pos = 10;  // strlen("DROP USER ")
    std::string user, host;

    if (!ParseUserHost(sql, pos, user, host)) {
        last_error_ = "Invalid DROP USER syntax. Expected: DROP USER 'user'@'host'";
        return -1;
    }

    if (!auth_mgr_->DropUser(user, host)) {
        last_error_ = "Failed to drop user '" + user + "'@'" + host + "' (may not exist)";
        return -1;
    }

    return 0;
}

int ExecutionEngine::ExecuteAlterUser(const std::string& sql,
                                       std::vector<Tuple>* /*results*/,
                                       const Schema** /*output_schema*/) {
    if (!auth_mgr_) {
        last_error_ = "AuthManager not available";
        return -1;
    }

    // Syntax: ALTER USER 'user'@'host' IDENTIFIED BY 'new_password'
    std::string upper = sql;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    size_t pos = 11;  // strlen("ALTER USER ")
    std::string user, host, password;

    if (!ParseUserHost(sql, pos, user, host)) {
        last_error_ = "Invalid ALTER USER syntax. Expected: ALTER USER 'user'@'host' IDENTIFIED BY 'new_password'";
        return -1;
    }

    size_t ident_pos = upper.find("IDENTIFIED BY", pos + (user.size() + host.size() + 6));
    if (ident_pos == std::string::npos) {
        last_error_ = "ALTER USER requires IDENTIFIED BY clause";
        return -1;
    }

    size_t pwd_start = ident_pos + 13;
    password = ParseQuotedString(sql, pwd_start);

    if (!auth_mgr_->AlterUserPassword(user, host, password)) {
        last_error_ = "Failed to alter user '" + user + "'@'" + host + "'";
        return -1;
    }

    return 0;
}

int ExecutionEngine::ExecuteSetPassword(const std::string& sql,
                                         std::vector<Tuple>* /*results*/,
                                         const Schema** /*output_schema*/) {
    if (!auth_mgr_) {
        last_error_ = "AuthManager not available";
        return -1;
    }

    // Syntax: SET PASSWORD FOR 'user'@'host' = 'new_password'
    // or:     SET PASSWORD = 'new_password'  (for current user)
    std::string upper = sql;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    std::string user = "root", host = "localhost";  // default
    std::string password;

    size_t for_pos = upper.find("FOR ");
    if (for_pos != std::string::npos) {
        size_t pos = for_pos + 4;  // strlen("FOR ")
        if (!ParseUserHost(sql, pos, user, host)) {
            last_error_ = "Invalid SET PASSWORD syntax. Expected: SET PASSWORD FOR 'user'@'host' = 'new_password'";
            return -1;
        }
    }

    size_t eq_pos = upper.find('=');
    if (eq_pos == std::string::npos) {
        last_error_ = "SET PASSWORD requires = 'password'";
        return -1;
    }

    size_t pwd_start = eq_pos + 1;
    password = ParseQuotedString(sql, pwd_start);

    if (!auth_mgr_->SetPassword(user, host, password)) {
        last_error_ = "Failed to set password for '" + user + "'@'" + host + "'";
        return -1;
    }

    return 0;
}

uint32_t ExecutionEngine::ParsePrivilegeList(const std::string& priv_list) {
    uint32_t privileges = 0;
    std::string upper = priv_list;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    if (upper.find("ALL") != std::string::npos && upper.find("ALL PRIVILEGES") == std::string::npos) {
        // Just "ALL" alone or "ALL PRIVILEGES"
    }

    // Split by comma and map each privilege
    std::stringstream ss(upper);
    std::string token;
    while (std::getline(ss, token, ',')) {
        // Trim whitespace
        size_t s = token.find_first_not_of(" \t\n\r");
        size_t e = token.find_last_not_of(" \t\n\r;");
        if (s == std::string::npos) continue;
        token = token.substr(s, e - s + 1);

        if (token == "ALL" || token == "ALL PRIVILEGES") {
            privileges |= static_cast<uint32_t>(Privilege::ALL);
        } else if (token == "SELECT") {
            privileges |= static_cast<uint32_t>(Privilege::SELECT);
        } else if (token == "INSERT") {
            privileges |= static_cast<uint32_t>(Privilege::INSERT);
        } else if (token == "UPDATE") {
            privileges |= static_cast<uint32_t>(Privilege::UPDATE);
        } else if (token == "DELETE") {
            privileges |= static_cast<uint32_t>(Privilege::DELETE);
        } else if (token == "CREATE") {
            privileges |= static_cast<uint32_t>(Privilege::CREATE);
        } else if (token == "DROP") {
            privileges |= static_cast<uint32_t>(Privilege::DROP);
        } else if (token == "INDEX") {
            privileges |= static_cast<uint32_t>(Privilege::INDEX);
        } else if (token == "ALTER") {
            privileges |= static_cast<uint32_t>(Privilege::ALTER);
        } else if (token == "GRANT OPTION") {
            privileges |= static_cast<uint32_t>(Privilege::GRANT);
        }
    }

    return privileges;
}

int ExecutionEngine::ExecuteGrant(const std::string& sql,
                                   std::vector<Tuple>* /*results*/,
                                   const Schema** /*output_schema*/) {
    if (!auth_mgr_) {
        last_error_ = "AuthManager not available";
        return -1;
    }

    // Syntax: GRANT privilege_list ON db.table TO 'user'@'host'
    //         GRANT privilege_list ON *.* TO 'user'@'host'
    //         GRANT privilege_list ON db.* TO 'user'@'host'
    std::string upper = sql;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    size_t grant_pos = upper.find("GRANT ") + 6;

    // Find ON
    size_t on_pos = upper.find(" ON ", grant_pos);
    if (on_pos == std::string::npos) {
        last_error_ = "GRANT syntax error: missing ON clause";
        return -1;
    }

    std::string priv_str = sql.substr(grant_pos, on_pos - grant_pos);
    uint32_t privileges = ParsePrivilegeList(priv_str);

    // Parse db.table after ON
    size_t to_pos = upper.find(" TO ", on_pos + 4);
    if (to_pos == std::string::npos) {
        last_error_ = "GRANT syntax error: missing TO clause";
        return -1;
    }

    std::string object = sql.substr(on_pos + 4, to_pos - on_pos - 4);
    // Trim
    size_t s = object.find_first_not_of(" \t\n\r");
    size_t e = object.find_last_not_of(" \t\n\r;");
    if (s != std::string::npos) object = object.substr(s, e - s + 1);

    // Split into db.table
    std::string db = "*", table = "*";
    size_t dot = object.find('.');
    if (dot != std::string::npos) {
        db = object.substr(0, dot);
        table = object.substr(dot + 1);
        // Trim
        s = db.find_first_not_of(" \t\n\r");
        e = db.find_last_not_of(" \t\n\r");
        if (s != std::string::npos) db = db.substr(s, e - s + 1);
        s = table.find_first_not_of(" \t\n\r");
        e = table.find_last_not_of(" \t\n\r");
        if (s != std::string::npos) table = table.substr(s, e - s + 1);
        // Unquote
        if (!db.empty() && (db.front() == '`' || db.front() == '\'')) db = db.substr(1, db.size() - 2);
        if (!table.empty() && (table.front() == '`' || table.front() == '\'')) table = table.substr(1, table.size() - 2);
    }

    // Parse user@host after TO
    size_t user_pos = to_pos + 4;
    std::string user, host;
    if (!ParseUserHost(sql, user_pos, user, host)) {
        last_error_ = "GRANT syntax error: invalid user@host format";
        return -1;
    }

    if (!auth_mgr_->GrantPrivilege(user, host, db, table, privileges)) {
        last_error_ = "Failed to grant privileges";
        return -1;
    }

    return 0;
}

int ExecutionEngine::ExecuteRevoke(const std::string& sql,
                                    std::vector<Tuple>* /*results*/,
                                    const Schema** /*output_schema*/) {
    if (!auth_mgr_) {
        last_error_ = "AuthManager not available";
        return -1;
    }

    // Syntax: REVOKE privilege_list ON db.table FROM 'user'@'host'
    std::string upper = sql;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    size_t revoke_pos = upper.find("REVOKE ") + 7;

    size_t on_pos = upper.find(" ON ", revoke_pos);
    if (on_pos == std::string::npos) {
        last_error_ = "REVOKE syntax error: missing ON clause";
        return -1;
    }

    std::string priv_str = sql.substr(revoke_pos, on_pos - revoke_pos);
    uint32_t privileges = ParsePrivilegeList(priv_str);

    size_t from_pos = upper.find(" FROM ", on_pos + 4);
    if (from_pos == std::string::npos) {
        last_error_ = "REVOKE syntax error: missing FROM clause";
        return -1;
    }

    std::string object = sql.substr(on_pos + 4, from_pos - on_pos - 4);
    size_t s = object.find_first_not_of(" \t\n\r");
    size_t e = object.find_last_not_of(" \t\n\r;");
    if (s != std::string::npos) object = object.substr(s, e - s + 1);

    std::string db = "*", table = "*";
    size_t dot = object.find('.');
    if (dot != std::string::npos) {
        db = object.substr(0, dot);
        table = object.substr(dot + 1);
        s = db.find_first_not_of(" \t\n\r");
        e = db.find_last_not_of(" \t\n\r");
        if (s != std::string::npos) db = db.substr(s, e - s + 1);
        s = table.find_first_not_of(" \t\n\r");
        e = table.find_last_not_of(" \t\n\r");
        if (s != std::string::npos) table = table.substr(s, e - s + 1);
        if (!db.empty() && (db.front() == '`' || db.front() == '\'')) db = db.substr(1, db.size() - 2);
        if (!table.empty() && (table.front() == '`' || table.front() == '\'')) table = table.substr(1, table.size() - 2);
    }

    size_t user_pos = from_pos + 6;
    std::string user, host;
    if (!ParseUserHost(sql, user_pos, user, host)) {
        last_error_ = "REVOKE syntax error: invalid user@host format";
        return -1;
    }

    if (!auth_mgr_->RevokePrivilege(user, host, db, table, privileges)) {
        last_error_ = "Failed to revoke privileges";
        return -1;
    }

    return 0;
}

int ExecutionEngine::ExecuteFlushPrivileges(std::vector<Tuple>* /*results*/,
                                             const Schema** /*output_schema*/) {
    if (!auth_mgr_) {
        last_error_ = "AuthManager not available";
        return -1;
    }

    auth_mgr_->FlushPrivileges();
    return 0;
}

// =============================================================================
// Transaction control command implementations
// =============================================================================

int ExecutionEngine::ExecuteStartTransaction(std::vector<Tuple>* /*results*/,
                                              const Schema** /*output_schema*/) {
    if (!txn_mgr_) {
        last_error_ = "TransactionManager not available";
        return -1;
    }

    Transaction* txn = txn_mgr_->Begin();
    if (!txn) {
        last_error_ = "Failed to start transaction";
        return -1;
    }

    return 0;
}

int ExecutionEngine::ExecuteCommit(std::vector<Tuple>* /*results*/,
                                    const Schema** /*output_schema*/) {
    if (!txn_mgr_) {
        last_error_ = "TransactionManager not available";
        return -1;
    }

    Transaction* txn = txn_mgr_->GetCurrentTransaction();
    if (!txn) {
        last_error_ = "No active transaction to commit";
        return -1;
    }

    if (!txn_mgr_->Commit(txn)) {
        last_error_ = "Failed to commit transaction";
        return -1;
    }

    return 0;
}

int ExecutionEngine::ExecuteRollback(std::vector<Tuple>* /*results*/,
                                      const Schema** /*output_schema*/) {
    if (!txn_mgr_) {
        last_error_ = "TransactionManager not available";
        return -1;
    }

    Transaction* txn = txn_mgr_->GetCurrentTransaction();
    if (!txn) {
        last_error_ = "No active transaction to rollback";
        return -1;
    }

    if (!txn_mgr_->Rollback(txn)) {
        last_error_ = "Failed to rollback transaction";
        return -1;
    }

    return 0;
}

int ExecutionEngine::ExecuteSavepoint(const std::string& sql,
                                       std::vector<Tuple>* /*results*/,
                                       const Schema** /*output_schema*/) {
    if (!txn_mgr_) {
        last_error_ = "TransactionManager not available";
        return -1;
    }

    // Syntax: SAVEPOINT <name>
    std::string upper = sql;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    size_t pos = upper.find("SAVEPOINT ") + 10;
    while (pos < sql.size() && isspace(static_cast<unsigned char>(sql[pos]))) pos++;

    std::string name;
    while (pos < sql.size() && !isspace(static_cast<unsigned char>(sql[pos])) && sql[pos] != ';')
        name += sql[pos++];

    if (name.empty()) {
        last_error_ = "SAVEPOINT requires a name";
        return -1;
    }

    Transaction* txn = txn_mgr_->GetCurrentTransaction();
    if (!txn) {
        last_error_ = "No active transaction for SAVEPOINT";
        return -1;
    }

    if (!txn_mgr_->CreateSavepoint(name)) {
        last_error_ = "Failed to create savepoint '" + name + "'";
        return -1;
    }

    return 0;
}

int ExecutionEngine::ExecuteReleaseSavepoint(const std::string& sql,
                                              std::vector<Tuple>* /*results*/,
                                              const Schema** /*output_schema*/) {
    if (!txn_mgr_) {
        last_error_ = "TransactionManager not available";
        return -1;
    }

    std::string upper = sql;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    size_t pos = upper.find("RELEASE SAVEPOINT ") + 18;
    while (pos < sql.size() && isspace(static_cast<unsigned char>(sql[pos]))) pos++;

    std::string name;
    while (pos < sql.size() && !isspace(static_cast<unsigned char>(sql[pos])) && sql[pos] != ';')
        name += sql[pos++];

    if (name.empty()) {
        last_error_ = "RELEASE SAVEPOINT requires a name";
        return -1;
    }

    if (!txn_mgr_->ReleaseSavepoint(name)) {
        last_error_ = "Savepoint '" + name + "' not found";
        return -1;
    }

    return 0;
}

int ExecutionEngine::ExecuteRollbackToSavepoint(const std::string& sql,
                                                 std::vector<Tuple>* /*results*/,
                                                 const Schema** /*output_schema*/) {
    if (!txn_mgr_) {
        last_error_ = "TransactionManager not available";
        return -1;
    }

    std::string upper = sql;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    size_t pos = upper.find("ROLLBACK TO SAVEPOINT ") + 22;
    while (pos < sql.size() && isspace(static_cast<unsigned char>(sql[pos]))) pos++;

    std::string name;
    while (pos < sql.size() && !isspace(static_cast<unsigned char>(sql[pos])) && sql[pos] != ';')
        name += sql[pos++];

    if (name.empty()) {
        last_error_ = "ROLLBACK TO SAVEPOINT requires a name";
        return -1;
    }

    if (!txn_mgr_->RollbackToSavepoint(name)) {
        last_error_ = "Savepoint '" + name + "' not found";
        return -1;
    }

    return 0;
}

int ExecutionEngine::ExecuteSetTransaction(const std::string& sql,
                                            std::vector<Tuple>* /*results*/,
                                            const Schema** /*output_schema*/) {
    if (!txn_mgr_) {
        last_error_ = "TransactionManager not available";
        return -1;
    }

    // Syntax: SET [SESSION] TRANSACTION ISOLATION LEVEL <level>
    std::string upper = sql;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    Transaction::IsolationLevel level = Transaction::IsolationLevel::READ_COMMITTED;

    if (upper.find("READ UNCOMMITTED") != std::string::npos) {
        level = Transaction::IsolationLevel::READ_UNCOMMITTED;
    } else if (upper.find("READ COMMITTED") != std::string::npos) {
        level = Transaction::IsolationLevel::READ_COMMITTED;
    } else if (upper.find("REPEATABLE READ") != std::string::npos) {
        level = Transaction::IsolationLevel::REPEATABLE_READ;
    } else if (upper.find("SERIALIZABLE") != std::string::npos) {
        level = Transaction::IsolationLevel::SERIALIZABLE;
    }

    txn_mgr_->SetDefaultIsolationLevel(level);
    return 0;
}

// =============================================================================
// Log management command implementations
// =============================================================================

int ExecutionEngine::ExecuteShowLogs(std::vector<Tuple>* /*results*/,
                                      const Schema** output_schema) {
    if (!log_mgr_) {
        last_error_ = "LogManager not available";
        return -1;
    }

    // Return list of log files
    static Schema show_logs_schema({
        Column("LogFile", TypeId::VARCHAR, 512),
        Column("Type", TypeId::VARCHAR, 32),
        Column("Size", TypeId::BIGINT),
    });

    if (output_schema) *output_schema = &show_logs_schema;

    // The LogManager manages three log files; we report them here
    // In production, this would list all files in the log directory
    // For now, report known log paths

    return 0;  // Success with empty result set (log paths are internal)
}

int ExecutionEngine::ExecuteShowBinlogEvents(const std::string& sql,
                                              std::vector<Tuple>* results,
                                              const Schema** output_schema) {
    if (!log_mgr_) {
        last_error_ = "LogManager not available";
        return -1;
    }

    static Schema binlog_events_schema({
        Column("LogName", TypeId::VARCHAR, 256),
        Column("Pos", TypeId::BIGINT),
        Column("EventType", TypeId::VARCHAR, 32),
        Column("ServerId", TypeId::INTEGER),
        Column("EndLogPos", TypeId::BIGINT),
        Column("Info", TypeId::VARCHAR, 1024),
    });

    if (output_schema) *output_schema = &binlog_events_schema;

    // Parse optional IN 'log_name', FROM pos, LIMIT n
    std::string upper = sql;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    std::string log_name;
    int64_t from_pos = 0;
    int64_t limit = -1;

    size_t in_pos = upper.find(" IN ");
    if (in_pos != std::string::npos) {
        size_t q_start = in_pos + 4;
        while (q_start < sql.size() && isspace(static_cast<unsigned char>(sql[q_start]))) q_start++;
        if (q_start < sql.size() && (sql[q_start] == '\'' || sql[q_start] == '"')) {
            char quote = sql[q_start++];
            while (q_start < sql.size() && sql[q_start] != quote) log_name += sql[q_start++];
        }
    }

    size_t from_p = upper.find(" FROM ");
    if (from_p != std::string::npos) {
        size_t num_start = from_p + 6;
        while (num_start < sql.size() && isspace(static_cast<unsigned char>(sql[num_start]))) num_start++;
        std::string num_str;
        while (num_start < sql.size() && isdigit(static_cast<unsigned char>(sql[num_start])))
            num_str += sql[num_start++];
        if (!num_str.empty()) from_pos = std::stoll(num_str);
    }

    size_t limit_p = upper.find(" LIMIT ");
    if (limit_p != std::string::npos) {
        size_t num_start = limit_p + 7;
        while (num_start < sql.size() && isspace(static_cast<unsigned char>(sql[num_start]))) num_start++;
        std::string num_str;
        while (num_start < sql.size() && isdigit(static_cast<unsigned char>(sql[num_start])))
            num_str += sql[num_start++];
        if (!num_str.empty()) limit = std::stoll(num_str);
    }

    // Read binlog events
    if (results) {
        auto events = log_mgr_->GetBinlogWriter().ReadEvents(log_name, from_pos, limit);
        for (const auto& ev : events) {
            std::vector<Value> row;
            row.push_back(Value::CreateVarchar(ev.log_name));
            row.push_back(Value::CreateBigInt(ev.position));
            row.push_back(Value::CreateVarchar(ev.event_type));
            row.push_back(Value::CreateInteger(static_cast<int32_t>(ev.server_id)));
            row.push_back(Value::CreateBigInt(static_cast<int64_t>(ev.end_log_pos)));
            row.push_back(Value::CreateVarchar(ev.info));
            results->push_back(Tuple::CreateFromValues(row, &binlog_events_schema));
        }
    }

    return 0;
}

int ExecutionEngine::ExecuteShowMasterStatus(std::vector<Tuple>* results,
                                              const Schema** output_schema) {
    if (!log_mgr_) {
        last_error_ = "LogManager not available";
        return -1;
    }

    static Schema master_status_schema({
        Column("File", TypeId::VARCHAR, 256),
        Column("Position", TypeId::BIGINT),
        Column("Binlog_Do_DB", TypeId::VARCHAR, 256),
        Column("Binlog_Ignore_DB", TypeId::VARCHAR, 256),
    });

    if (output_schema) *output_schema = &master_status_schema;

    if (results) {
        std::string current_file = log_mgr_->GetBinlogWriter().GetCurrentFileName();
        uint64_t current_pos = log_mgr_->GetBinlogWriter().GetCurrentPosition();

        std::vector<Value> row;
        row.push_back(Value::CreateVarchar(current_file));
        row.push_back(Value::CreateBigInt(static_cast<int64_t>(current_pos)));
        row.push_back(Value::CreateVarchar(""));
        row.push_back(Value::CreateVarchar(""));
        results->push_back(Tuple::CreateFromValues(row, &master_status_schema));
    }

    return 0;
}

int ExecutionEngine::ExecuteFlushLogs(std::vector<Tuple>* /*results*/,
                                       const Schema** /*output_schema*/) {
    if (!log_mgr_) {
        last_error_ = "LogManager not available";
        return -1;
    }

    // Force rotate all log files
    log_mgr_->GetBinlogWriter().Rotate();
    log_mgr_->GetQueryLog().Rotate();
    return 0;
}

int ExecutionEngine::ExecutePurgeBinaryLogs(const std::string& sql,
                                             std::vector<Tuple>* /*results*/,
                                             const Schema** /*output_schema*/) {
    if (!log_mgr_) {
        last_error_ = "LogManager not available";
        return -1;
    }

    // Syntax: PURGE BINARY LOGS TO 'log_name'
    std::string upper = sql;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    size_t to_pos = upper.find(" TO ");
    if (to_pos == std::string::npos) {
        last_error_ = "PURGE BINARY LOGS requires TO clause";
        return -1;
    }

    size_t q_start = to_pos + 4;
    std::string log_name = ParseQuotedString(sql, q_start);

    if (log_name.empty()) {
        last_error_ = "Invalid log name in PURGE BINARY LOGS";
        return -1;
    }

    log_mgr_->GetBinlogWriter().PurgeTo(log_name);
    return 0;
}

int ExecutionEngine::ExecuteResetMaster(std::vector<Tuple>* /*results*/,
                                         const Schema** /*output_schema*/) {
    if (!log_mgr_) {
        last_error_ = "LogManager not available";
        return -1;
    }

    log_mgr_->GetBinlogWriter().Reset();
    return 0;
}

// =============================================================================
// New administrative command handlers (Day 2)
// =============================================================================

int ExecutionEngine::ExecuteShutdown(std::vector<Tuple>* /*results*/,
                                      const Schema** /*output_schema*/) {
    if (!shutdown_callback_) {
        last_error_ = "Shutdown not supported (no callback registered)";
        return -1;
    }
    shutdown_callback_();
    return 0;
}

int ExecutionEngine::ExecuteShowStatus(std::vector<Tuple>* results,
                                        const Schema** output_schema) {
    static Schema status_schema({
        Column("Variable_name", TypeId::VARCHAR, 128),
        Column("Value", TypeId::VARCHAR, 256),
    });
    if (output_schema) *output_schema = &status_schema;
    if (!results) return 0;

    auto add_var = [&](const std::string& name, const std::string& val) {
        std::vector<Value> row;
        row.push_back(Value::CreateVarchar(name));
        row.push_back(Value::CreateVarchar(val));
        results->push_back(Tuple::CreateFromValues(row, &status_schema));
    };

    // Uptime
    auto now = std::chrono::steady_clock::now();
    int64_t uptime = std::chrono::duration_cast<std::chrono::seconds>(
        now - server_start_time_).count();
    add_var("uptime", std::to_string(uptime));

    // Queries
    uint64_t qc = query_counter_.load();
    add_var("queries", std::to_string(qc));
    add_var("questions", std::to_string(qc));
    add_var("queries_per_second",
            uptime > 0 ? std::to_string(static_cast<double>(qc) / uptime) : "0");

    // Connections
    uint64_t active = Connection::GetActiveConnectionCount();
    uint64_t total = Connection::GetTotalConnections();
    add_var("threads_connected", std::to_string(active));
    add_var("max_connections", "151");
    add_var("max_used_connections", std::to_string(total));

    // Bytes transferred
    add_var("bytes_received",
            std::to_string(Connection::GetTotalBytesReceived()));
    add_var("bytes_sent",
            std::to_string(Connection::GetTotalBytesSent()));

    // Buffer pool
    if (bpm_) {
        add_var("buffer_pool_size", std::to_string(bpm_->GetPoolSize()));
        add_var("buffer_pool_pages_free",
                std::to_string(bpm_->GetFreeFrameCount()));
        add_var("buffer_pool_pages_dirty",
                std::to_string(bpm_->GetDirtyPageCount()));
        add_var("buffer_pool_hit_rate",
                std::to_string(bpm_->GetHitRate() * 100) + "%");
        add_var("buffer_pool_read_requests",
                std::to_string(qc > 0 ? qc : 1));
        add_var("buffer_pool_reads", "0");
    } else {
        add_var("buffer_pool_size", "0");
        add_var("buffer_pool_pages_free", "0");
        add_var("buffer_pool_hit_rate", "0.0%");
        add_var("buffer_pool_dirty_pages", "0");
    }

    add_var("version", "goods_db v0.1.0");
    add_var("server_id", "1");

    return 0;
}

int ExecutionEngine::ExecuteShowProcesslist(std::vector<Tuple>* results,
                                             const Schema** output_schema) {
    static Schema proc_schema({
        Column("Id", TypeId::INTEGER),
        Column("User", TypeId::VARCHAR, 64),
        Column("Host", TypeId::VARCHAR, 128),
        Column("db", TypeId::VARCHAR, 128),
        Column("Command", TypeId::VARCHAR, 32),
        Column("Time", TypeId::INTEGER),
        Column("State", TypeId::VARCHAR, 64),
        Column("Info", TypeId::VARCHAR, 1024),
    });
    if (output_schema) *output_schema = &proc_schema;
    if (!results) return 0;

    std::lock_guard<std::mutex> lock(ConnectionHandler::GetRegistryMutex());
    for (const auto& [id, handler] : ConnectionHandler::GetRegistry()) {
        auto* conn = handler->GetConnection();
        if (!conn) continue;

        std::vector<Value> row;
        row.push_back(Value::CreateInteger(static_cast<int32_t>(id)));
        row.push_back(Value::CreateVarchar(conn->GetUser()));
        row.push_back(Value::CreateVarchar(
            conn->GetRemoteAddr() + ":" + std::to_string(conn->GetRemotePort())));
        row.push_back(Value::CreateVarchar(conn->GetDatabase()));

        // Map connection state to command name
        std::string cmd;
        switch (conn->GetState()) {
            case Connection::State::INIT:     cmd = "Connect"; break;
            case Connection::State::AUTH:     cmd = "Authenticate"; break;
            case Connection::State::READY:    cmd = "Sleep"; break;
            case Connection::State::QUERYING: cmd = "Query"; break;
            case Connection::State::SENDING:  cmd = "Send"; break;
            case Connection::State::CLOSING:  cmd = "Close"; break;
            default: cmd = "Unknown"; break;
        }
        row.push_back(Value::CreateVarchar(cmd));
        row.push_back(Value::CreateInteger(0));  // Time (seconds in current state)

        // State detail
        std::string state_str;
        switch (conn->GetState()) {
            case Connection::State::QUERYING: state_str = "executing"; break;
            case Connection::State::READY:    state_str = ""; break;
            case Connection::State::SENDING:  state_str = "Writing to net"; break;
            default: state_str = ""; break;
        }
        row.push_back(Value::CreateVarchar(state_str));

        // Currently executing query
        row.push_back(Value::CreateVarchar(handler->GetCurrentQuery()));

        results->push_back(Tuple::CreateFromValues(row, &proc_schema));
    }
    return 0;
}

int ExecutionEngine::ExecuteFlushHosts(std::vector<Tuple>* /*results*/,
                                        const Schema** /*output_schema*/) {
    if (!auth_mgr_) {
        last_error_ = "AuthManager not available";
        return -1;
    }
    auth_mgr_->ClearBlockList();
    return 0;
}

int ExecutionEngine::ExecuteFlushTables(std::vector<Tuple>* /*results*/,
                                         const Schema** /*output_schema*/) {
    // No-op: no table open cache to flush in this implementation.
    // Flush buffer pool dirty pages to disk as a best-effort operation.
    if (bpm_) {
        bpm_->FlushAllPages();
    }
    return 0;
}

int ExecutionEngine::ExecuteKill(const std::string& sql,
                                  std::vector<Tuple>* /*results*/,
                                  const Schema** /*output_schema*/) {
    // Parse: KILL <connection_id>
    size_t pos = 5;  // strlen("KILL ")
    while (pos < sql.size() && isspace(static_cast<unsigned char>(sql[pos]))) pos++;
    std::string id_str;
    while (pos < sql.size() && isdigit(static_cast<unsigned char>(sql[pos])))
        id_str += sql[pos++];

    if (id_str.empty()) {
        last_error_ = "KILL requires a connection ID";
        return -1;
    }

    uint32_t target_id;
    try {
        target_id = static_cast<uint32_t>(std::stoul(id_str));
    } catch (...) {
        last_error_ = "Invalid connection ID: " + id_str;
        return -1;
    }

    ConnectionHandler* target = nullptr;
    {
        std::lock_guard<std::mutex> lock(ConnectionHandler::GetRegistryMutex());
        auto& registry = ConnectionHandler::GetRegistry();
        auto it = registry.find(target_id);
        if (it != registry.end()) {
            target = it->second;
            registry.erase(it);  // remove before Stop() to prevent double-erase
        }
    }

    if (!target) {
        last_error_ = "Unknown connection ID: " + id_str;
        return -1;
    }

    target->Stop();
    return 0;
}

int ExecutionEngine::ExecuteShowUsers(std::vector<Tuple>* results,
                                       const Schema** output_schema) {
    if (!auth_mgr_) {
        last_error_ = "AuthManager not available";
        return -1;
    }

    static Schema users_schema({
        Column("user", TypeId::VARCHAR, 64),
        Column("host", TypeId::VARCHAR, 128),
        Column("created", TypeId::VARCHAR, 128),
    });
    if (output_schema) *output_schema = &users_schema;
    if (!results) return 0;

    auto users = auth_mgr_->GetUsers();
    for (const auto& u : users) {
        std::vector<Value> row;
        row.push_back(Value::CreateVarchar(u.user));
        row.push_back(Value::CreateVarchar(u.host));
        row.push_back(Value::CreateVarchar(""));
        results->push_back(Tuple::CreateFromValues(row, &users_schema));
    }
    return 0;
}

int ExecutionEngine::ExecuteShowGrants(const std::string& sql,
                                        std::vector<Tuple>* results,
                                        const Schema** output_schema) {
    if (!auth_mgr_) {
        last_error_ = "AuthManager not available";
        return -1;
    }

    // Parse: SHOW GRANTS FOR 'user'@'host'
    std::string upper = sql;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    size_t for_pos = upper.find(" FOR ");
    if (for_pos == std::string::npos) {
        last_error_ = "SHOW GRANTS requires FOR clause";
        return -1;
    }

    size_t parse_pos = for_pos + 5;  // strlen(" FOR ")
    std::string user, host;
    if (!ParseUserHost(sql, parse_pos, user, host)) {
        last_error_ = "Invalid user@host format in SHOW GRANTS";
        return -1;
    }

    static Schema grants_schema({
        Column("Grants", TypeId::VARCHAR, 1024),
    });
    if (output_schema) *output_schema = &grants_schema;
    if (!results) return 0;

    auto privs = auth_mgr_->GetPrivileges();

    // Group privileges by (db, table) for the target user
    std::map<std::pair<std::string, std::string>, uint32_t> grouped;
    for (const auto& p : privs) {
        if (p.user == user && (p.host == host || p.host == "%")) {
            grouped[{p.db, p.table_name}] |= p.privileges;
        }
    }

    // Privilege name mapping
    static const std::pair<Privilege, const char*> kPrivNames[] = {
        {Privilege::SELECT, "SELECT"}, {Privilege::INSERT, "INSERT"},
        {Privilege::UPDATE, "UPDATE"}, {Privilege::DELETE, "DELETE"},
        {Privilege::CREATE, "CREATE"}, {Privilege::DROP,   "DROP"},
        {Privilege::INDEX,  "INDEX"},  {Privilege::ALTER,  "ALTER"},
        {Privilege::GRANT,  "GRANT OPTION"},
    };

    for (const auto& [key, priv_mask] : grouped) {
        const auto& [db, table] = key;
        std::string priv_str;
        if ((priv_mask & static_cast<uint32_t>(Privilege::ALL)) ==
            static_cast<uint32_t>(Privilege::ALL)) {
            priv_str = "ALL PRIVILEGES";
        } else {
            for (const auto& [p, name] : kPrivNames) {
                if (priv_mask & static_cast<uint32_t>(p)) {
                    if (!priv_str.empty()) priv_str += ", ";
                    priv_str += name;
                }
            }
        }
        if (priv_str.empty()) priv_str = "USAGE";

        std::string grant = "GRANT " + priv_str + " ON " + db + "." + table +
                            " TO '" + user + "'@'" + host + "'";

        std::vector<Value> row;
        row.push_back(Value::CreateVarchar(grant));
        results->push_back(Tuple::CreateFromValues(row, &grants_schema));
    }

    if (results->empty()) {
        std::vector<Value> row;
        row.push_back(Value::CreateVarchar(
            "GRANT USAGE ON *.* TO '" + user + "'@'" + host + "'"));
        results->push_back(Tuple::CreateFromValues(row, &grants_schema));
    }

    return 0;
}

int ExecutionEngine::ExecuteShowErrorLog(const std::string& sql,
                                          std::vector<Tuple>* results,
                                          const Schema** output_schema) {
    if (!log_mgr_) {
        last_error_ = "LogManager not available";
        return -1;
    }

    static Schema errorlog_schema({
        Column("time", TypeId::VARCHAR, 32),
        Column("level", TypeId::VARCHAR, 8),
        Column("code", TypeId::INTEGER),
        Column("message", TypeId::VARCHAR, 1024),
        Column("source", TypeId::VARCHAR, 256),
    });
    if (output_schema) *output_schema = &errorlog_schema;
    if (!results) return 0;

    // Parse optional level filter: WHERE level = 'ERROR' or AND level = 'ERROR'
    std::string upper = sql;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    std::string level_filter;
    size_t level_pos = upper.find("LEVEL = '");
    if (level_pos == std::string::npos) level_pos = upper.find("LEVEL='");
    if (level_pos != std::string::npos) {
        size_t q_start = upper.find('\'', level_pos);
        if (q_start != std::string::npos) {
            size_t q_end = upper.find('\'', q_start + 1);
            if (q_end != std::string::npos) {
                level_filter = sql.substr(q_start + 1, q_end - q_start - 1);
            }
        }
    }

    // Parse optional keyword filter: WHERE message LIKE '%keyword%' or AND message LIKE '%keyword%'
    std::string keyword;
    size_t like_pos = upper.find("LIKE '%");
    if (like_pos != std::string::npos) {
        size_t kw_start = like_pos + 7;  // strlen("LIKE '%")
        size_t kw_end = upper.find("%'", kw_start);
        if (kw_end != std::string::npos) {
            keyword = sql.substr(kw_start, kw_end - kw_start);
        }
    }

    // Parse LIMIT
    size_t max_count = 200;
    size_t limit_pos = upper.find("LIMIT ");
    if (limit_pos != std::string::npos) {
        std::string num_str;
        size_t num_start = limit_pos + 6;
        while (num_start < sql.size() && isspace(static_cast<unsigned char>(sql[num_start]))) num_start++;
        while (num_start < sql.size() && isdigit(static_cast<unsigned char>(sql[num_start])))
            num_str += sql[num_start++];
        if (!num_str.empty()) {
            try { max_count = std::stoull(num_str); } catch (...) {}
        }
    }

    auto entries = log_mgr_->GetErrorLog().GetRecentEntries(max_count);

    for (const auto& e : entries) {
        // Apply level filter
        if (!level_filter.empty()) {
            std::string entry_level = e.level;
            for (auto& c : entry_level) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
            if (entry_level != level_filter) continue;
        }
        // Apply keyword filter
        if (!keyword.empty()) {
            if (e.message.find(keyword) == std::string::npos) continue;
        }

        std::vector<Value> row;
        row.push_back(Value::CreateVarchar(e.timestamp));
        row.push_back(Value::CreateVarchar(e.level));
        row.push_back(Value::CreateInteger(e.code));
        row.push_back(Value::CreateVarchar(e.message));
        row.push_back(Value::CreateVarchar(e.source));
        results->push_back(Tuple::CreateFromValues(row, &errorlog_schema));
    }

    return 0;
}

int ExecutionEngine::ExecuteShowQueryLog(const std::string& sql,
                                          std::vector<Tuple>* results,
                                          const Schema** output_schema) {
    if (!log_mgr_) {
        last_error_ = "LogManager not available";
        return -1;
    }

    static Schema querylog_schema({
        Column("time", TypeId::VARCHAR, 32),
        Column("user", TypeId::VARCHAR, 64),
        Column("host", TypeId::VARCHAR, 128),
        Column("database", TypeId::VARCHAR, 128),
        Column("duration", TypeId::VARCHAR, 32),
        Column("rows", TypeId::BIGINT),
        Column("query", TypeId::VARCHAR, 4096),
    });
    if (output_schema) *output_schema = &querylog_schema;
    if (!results) return 0;

    // Parse LIMIT
    std::string upper = sql;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    size_t max_count = 200;
    size_t limit_pos = upper.find("LIMIT ");
    if (limit_pos != std::string::npos) {
        std::string num_str;
        size_t num_start = limit_pos + 6;
        while (num_start < sql.size() && isspace(static_cast<unsigned char>(sql[num_start]))) num_start++;
        while (num_start < sql.size() && isdigit(static_cast<unsigned char>(sql[num_start])))
            num_str += sql[num_start++];
        if (!num_str.empty()) {
            try { max_count = std::stoull(num_str); } catch (...) {}
        }
    }

    auto entries = log_mgr_->GetQueryLog().GetRecentEntries(max_count);

    for (const auto& e : entries) {
        std::vector<Value> row;
        row.push_back(Value::CreateVarchar(e.timestamp));
        row.push_back(Value::CreateVarchar(e.user));
        row.push_back(Value::CreateVarchar(e.host));
        row.push_back(Value::CreateVarchar(e.database));
        row.push_back(Value::CreateVarchar(
            std::to_string(e.exec_time_ms) + "ms"));
        row.push_back(Value::CreateBigInt(
            static_cast<int64_t>(e.rows_affected)));
        row.push_back(Value::CreateVarchar(e.sql));
        results->push_back(Tuple::CreateFromValues(row, &querylog_schema));
    }

    return 0;
}

int ExecutionEngine::ExecuteShowBinaryLog(const std::string& /*sql*/,
                                           std::vector<Tuple>* results,
                                           const Schema** output_schema) {
    if (!log_mgr_) {
        last_error_ = "LogManager not available";
        return -1;
    }

    static Schema binlog_schema({
        Column("LogName", TypeId::VARCHAR, 256),
        Column("Size", TypeId::BIGINT),
        Column("Records", TypeId::INTEGER),
    });
    if (output_schema) *output_schema = &binlog_schema;
    if (!results) return 0;

    auto files = log_mgr_->GetBinlogWriter().ListBinlogFiles();
    for (const auto& f : files) {
        std::vector<Value> row;
        row.push_back(Value::CreateVarchar(f));
        row.push_back(Value::CreateBigInt(0));
        row.push_back(Value::CreateInteger(0));
        results->push_back(Tuple::CreateFromValues(row, &binlog_schema));
    }

    return 0;
}

// =============================================================================
// Permission enforcement helpers
// =============================================================================

std::string ExecutionEngine::ExtractTableName(const ASTNode* node) {
    if (!node) return "*";

    // Try to extract the table name from the statement via dynamic_cast
    // We do this by walking through the known statement types
    auto* select = dynamic_cast<const SelectStatement*>(node);
    if (select) return select->table_name.empty() ? "*" : select->table_name;

    auto* insert = dynamic_cast<const InsertStatement*>(node);
    if (insert) return insert->table_name.empty() ? "*" : insert->table_name;

    auto* update = dynamic_cast<const UpdateStatement*>(node);
    if (update) return update->table_name.empty() ? "*" : update->table_name;

    auto* del = dynamic_cast<const DeleteStatement*>(node);
    if (del) return del->table_name.empty() ? "*" : del->table_name;

    auto* create = dynamic_cast<const CreateStatement*>(node);
    if (create) return create->table_name.empty() ? "*" : create->table_name;

    auto* drop = dynamic_cast<const DropStatement*>(node);
    if (drop) return drop->table_name.empty() ? "*" : drop->table_name;

    auto* create_idx = dynamic_cast<const CreateIndexStatement*>(node);
    if (create_idx) return create_idx->table_name.empty() ? "*" : create_idx->table_name;

    return "*";
}

Privilege ExecutionEngine::GetRequiredPrivilege(uint8_t node_type) {
    switch (static_cast<ASTNodeType>(node_type)) {
        case ASTNodeType::STATEMENT_SELECT:
            return Privilege::SELECT;
        case ASTNodeType::STATEMENT_INSERT:
            return Privilege::INSERT;
        case ASTNodeType::STATEMENT_UPDATE:
            return Privilege::UPDATE;
        case ASTNodeType::STATEMENT_DELETE:
            return Privilege::DELETE;
        case ASTNodeType::STATEMENT_CREATE:
            return Privilege::CREATE;
        case ASTNodeType::STATEMENT_DROP:
            return Privilege::DROP;
        case ASTNodeType::STATEMENT_CREATE_INDEX:
            return Privilege::INDEX;
        default:
            return Privilege::ALL;  // Unknown — require all
    }
}

bool ExecutionEngine::CheckPrivilegeForStatement(Privilege required,
                                                  const std::string& table_name) {
    // No auth manager or no current user — allow (backward compat)
    if (!auth_mgr_ || current_user_.empty()) return true;

    // root always has all privileges
    if (current_user_ == "root") return true;

    // Use "goods_db" as the default database name
    bool ok = auth_mgr_->CheckAccess(current_user_, current_host_,
                                      "goods_db", table_name, required);
    if (!ok) {
        last_error_ = "Access denied for user '" + current_user_ +
                      "' to table '" + table_name +
                      "' (requires " + PrivilegeToString(required) + ")";
    }
    return ok;
}

bool ExecutionEngine::CheckAdminAccess() {
    // No auth manager or no current user — allow (backward compat)
    if (!auth_mgr_ || current_user_.empty()) return true;

    // root always has all privileges
    if (current_user_ == "root") return true;

    // Admin commands require GRANT privilege on *.*
    bool ok = auth_mgr_->CheckAccess(current_user_, current_host_,
                                      "goods_db", "*", Privilege::GRANT);
    if (!ok) {
        last_error_ = "Access denied for user '" + current_user_ +
                      "' — GRANT privilege required for administrative commands";
    }
    return ok;
}

int ExecutionEngine::ExecuteRegisterFk(const std::string& sql,
                                        std::vector<Tuple>* /*results*/,
                                        const Schema** /*output_schema*/) {
    // Format: REGISTER_FK parent_table.parent_column child_table.child_column [CASCADE|RESTRICT|SET_NULL]
    // Example: REGISTER_FK warehouses.id inventory.warehouse_id CASCADE
    std::string args = sql.substr(12);  // skip "REGISTER_FK "
    // Trim leading/trailing whitespace
    size_t start = args.find_first_not_of(" \t");
    size_t end = args.find_last_not_of(" \t");
    if (start == std::string::npos || end == std::string::npos) {
        last_error_ = "REGISTER_FK: missing arguments";
        return -1;
    }
    args = args.substr(start, end - start + 1);

    // Split by whitespace
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos < args.size()) {
        size_t next = args.find_first_of(" \t", pos);
        if (next == std::string::npos) {
            parts.push_back(args.substr(pos));
            break;
        }
        parts.push_back(args.substr(pos, next - pos));
        pos = args.find_first_not_of(" \t", next);
        if (pos == std::string::npos) break;
    }

    if (parts.size() < 2) {
        last_error_ = "REGISTER_FK: expected at least 2 arguments";
        return -1;
    }

    // Parse parent_table.parent_column
    std::string parent_table, parent_column;
    size_t dot1 = parts[0].find('.');
    if (dot1 == std::string::npos) {
        last_error_ = "REGISTER_FK: expected parent_table.parent_column format";
        return -1;
    }
    parent_table = parts[0].substr(0, dot1);
    parent_column = parts[0].substr(dot1 + 1);

    // Parse child_table.child_column
    std::string child_table, child_column;
    size_t dot2 = parts[1].find('.');
    if (dot2 == std::string::npos) {
        last_error_ = "REGISTER_FK: expected child_table.child_column format";
        return -1;
    }
    child_table = parts[1].substr(0, dot2);
    child_column = parts[1].substr(dot2 + 1);

    // Parse optional action (default: CASCADE)
    FkAction action = FkAction::CASCADE;
    if (parts.size() >= 3) {
        std::string action_str = parts[2];
        for (auto& c : action_str) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
        if (action_str == "RESTRICT") action = FkAction::RESTRICT;
        else if (action_str == "SET_NULL" || action_str == "SETNULL") action = FkAction::SET_NULL;
        else if (action_str != "CASCADE") {
            last_error_ = "REGISTER_FK: unknown action '" + parts[2] + "'";
            return -1;
        }
    }

    if (!catalog_) {
        last_error_ = "REGISTER_FK: no catalog available";
        return -1;
    }

    bool ok = catalog_->RegisterForeignKey(parent_table, parent_column,
                                           child_table, child_column, action);
    if (!ok) {
        last_error_ = "REGISTER_FK: failed to register FK (table not found?)";
        return -1;
    }

    return 0;
}

}  // namespace goods_db
