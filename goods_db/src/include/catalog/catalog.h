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
 * Catalog - manages all metadata about tables and indexes.
 *
 * Stores table definitions, schemas, and index metadata.
 * On startup, loads metadata from the catalog file (goods_db_catalog.db).
 */
class Catalog {
public:
    explicit Catalog(BufferPoolManager* bpm);
    ~Catalog() = default;

    // =========================================================================
    // Table Operations
    // =========================================================================

    /**
     * Create a new table and register it in the catalog.
     * @return true on success, false if table already exists
     */
    bool CreateTable(const std::string& name, const Schema& schema,
                     const std::string& file_path, page_id_t root_page_id);

    /**
     * Get table metadata by name.
     * @return TableInfo pointer, or nullptr if not found
     */
    TableInfo* GetTable(const std::string& name);

    /**
     * Check if a table exists.
     */
    bool TableExists(const std::string& name) const;

    /**
     * Drop a table from the catalog.
     * @return true on success
     */
    bool DropTable(const std::string& name);

    /**
     * List all table names.
     */
    std::vector<std::string> ListTables() const;

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
     * Get an index by name.
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
    std::map<std::string, TableInfo> tables_;
    mutable std::mutex mutex_;
};

}  // namespace goods_db
