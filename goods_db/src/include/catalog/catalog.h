#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "buffer/buffer_pool_manager.h"
#include "storage/index/b_plus_tree.h"
#include "storage/index/extendible_hash_index.h"
#include "storage/table/table_heap.h"
#include "type/schema.h"

namespace goods_db {

/** Default database name */
constexpr const char* DEFAULT_DATABASE = "goods_db";

/**
 * Index information stored in the catalog.
 */
struct IndexInfo {
    std::string index_name;
    std::string table_name;
    std::string column_name;
    enum class IndexType { BTREE, HASH } type;
    page_id_t root_or_directory_page_id;  // root for B+Tree, directory for Hash
    std::unique_ptr<Index> index_instance;
};

/**
 * Table information stored in the catalog.
 */
struct TableInfo {
    std::string table_name;
    Schema schema;
    std::string file_path;
    page_id_t root_page_id{INVALID_PAGE_ID};  // first page of the table
    std::vector<IndexInfo> indexes;
    std::unique_ptr<TableHeap> table_heap;
};

/**
 * Database info — a database is a namespace for tables.
 */
struct DatabaseInfo {
    std::string database_name;
    std::map<std::string, TableInfo> tables;
};

/**
 * Catalog - manages all metadata about databases, tables and indexes.
 *
 * Hierarchical: Database → Table → Index
 * On startup, loads metadata from the catalog file (goods_db_catalog.db).
 */
class Catalog {
public:
    explicit Catalog(BufferPoolManager* bpm);
    ~Catalog() = default;

    // =========================================================================
    // Database Operations
    // =========================================================================

    /**
     * Create a new database.
     * @param name Database name (case-insensitive match for uniqueness)
     * @param if_not_exists If true, silently succeed if database already exists
     * @return true on success
     */
    bool CreateDatabase(const std::string& name, bool if_not_exists = false);

    /**
     * Drop a database and all its tables.
     * @param name Database name
     * @param if_exists If true, silently succeed if database does not exist
     * @return true on success
     */
    bool DropDatabase(const std::string& name, bool if_exists = false);

    /**
     * Check if a database exists.
     */
    bool DatabaseExists(const std::string& name) const;

    /**
     * List all database names.
     */
    std::vector<std::string> ListDatabases() const;

    /**
     * Get the default database name.
     */
    const std::string& GetDefaultDatabase() const { return default_db_; }

    // =========================================================================
    // Table Operations (operate on the default database)
    // =========================================================================

    /**
     * Create a new table in the default database.
     * @return true on success, false if table already exists
     */
    bool CreateTable(const std::string& name, const Schema& schema,
                     const std::string& file_path, page_id_t root_page_id);

    /**
     * Get table metadata by name from the default database.
     * Also searches all databases if not found in default (for backward compatibility).
     * @return TableInfo pointer, or nullptr if not found
     */
    TableInfo* GetTable(const std::string& name);

    /**
     * Check if a table exists in the default database.
     */
    bool TableExists(const std::string& name) const;

    /**
     * Drop a table from the default database.
     * @return true on success
     */
    bool DropTable(const std::string& name);

    /**
     * List all table names in the default database.
     */
    std::vector<std::string> ListTables() const;

    /**
     * Get references to all table infos from all databases (for SHOW TABLES etc.)
     */
    std::vector<const TableInfo*> GetAllTables() const;

    // =========================================================================
    // Index Operations
    // =========================================================================

    /**
     * Create an index on a table column.
     * @return true on success
     */
    bool CreateIndex(const std::string& index_name, const std::string& table_name,
                     const std::string& column_name,
                     IndexInfo::IndexType type,
                     page_id_t root_or_directory_page_id);

    /**
     * Get an index by name (searches all databases).
     */
    IndexInfo* GetIndex(const std::string& index_name);

    /**
     * Get all indexes for a table.
     */
    std::vector<IndexInfo*> GetTableIndexes(const std::string& table_name);

    /**
     * Drop an index.
     */
    bool DropIndex(const std::string& index_name);

    // =========================================================================
    // Persistence
    // =========================================================================

    /**
     * Load catalog from the catalog database file.
     * @return true on success
     */
    bool Load();

    /**
     * Persist catalog to the catalog database file.
     * @return true on success
     */
    bool Save();

    /** Get the buffer pool manager */
    BufferPoolManager* GetBufferPoolManager() const { return bpm_; }

private:
    BufferPoolManager* bpm_;
    std::string default_db_;
    std::map<std::string, DatabaseInfo> databases_;
    mutable std::mutex mutex_;

    /** Normalize a database name for case-insensitive lookup */
    static std::string NormalizeName(const std::string& name);

    /** Get the DatabaseInfo for the default database, creating it if needed */
    DatabaseInfo* GetOrCreateDefaultDB();
};

}  // namespace goods_db
