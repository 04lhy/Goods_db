#pragma once

#include <memory>
#include <string>
#include <vector>
#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "catalog/catalog.h"
#include "sql/handler.h"
#include "sql/handlerton.h"
#include "storage/disk/disk_manager.h"
#include "storage/index/b_plus_tree.h"
#include "storage/index/extendible_hash_index.h"
#include "storage/table/table_heap.h"
#include "storage/table/table_iterator.h"
#include "type/schema.h"
#include "type/value.h"

namespace goods_db {

/**
 * goods_handler - the concrete storage engine handler for goods_db.
 *
 * Implements the handler abstract interface by delegating to:
 *   - TableHeap (full table scan, tuple CRUD)
 *   - BPlusTree / ExtendibleHashIndex (index operations)
 *   - DiskManager + BufferPoolManager (storage layer)
 *   - Catalog (metadata)
 */
class goods_handler : public handler {
public:
    goods_handler(BufferPoolManager* bpm, DiskManager* disk_manager,
                  Catalog* catalog);
    ~goods_handler() override;

    // =========================================================================
    // Table Lifecycle (DDL)
    // =========================================================================

    int create(const char* name, Schema* schema) override;
    int open(const char* name) override;
    int close() override;
    int delete_table(const char* name) override;

    // =========================================================================
    // Full Table Scan
    // =========================================================================

    int rnd_init(bool scan_all) override;
    int rnd_next(Tuple* buf) override;
    int rnd_pos(Tuple* buf, RID rid) override;
    int rnd_end() override;

    // =========================================================================
    // Index Scan
    // =========================================================================

    int index_init(uint32_t idx_id, bool sorted) override;
    int index_read(Tuple* buf, const Value* key) override;
    int index_next(Tuple* buf) override;
    int index_end() override;

    // =========================================================================
    // Row-level DML
    // =========================================================================

    int write_row(const Tuple& tuple) override;
    int update_row(const RID& rid, const Tuple& new_tuple) override;
    int delete_row(const RID& rid) override;

    // =========================================================================
    // Information
    // =========================================================================

    uint64_t records() override;
    uint64_t table_flags() const override;
    const char* get_table_name() const override;
    const Schema* get_schema() const override;

    // =========================================================================
    // Engine registration helpers
    // =========================================================================

    /** Initialize the goods_engine */
    static int engine_init();

    /** Deinitialize the goods_engine */
    static int engine_deinit();

    /** Factory: create a goods_handler */
    static handler* engine_create_handler(const char* table_name, Schema* schema);

    /** Get the shared BufferPoolManager */
    static BufferPoolManager* GetSharedBPM();

    /** Get the shared DiskManager */
    static DiskManager* GetSharedDiskManager();

    /** Get the shared Catalog */
    static Catalog* GetSharedCatalog();

private:
    BufferPoolManager* bpm_;
    DiskManager* disk_manager_;
    Catalog* catalog_;

    // Current table state
    std::string table_name_;
    const Schema* schema_{nullptr};
    TableInfo* table_info_{nullptr};
    TableHeap* table_heap_{nullptr};

    // Iterator state
    std::unique_ptr<TableIterator> table_iterator_;
    bool scan_active_{false};

    // Index scan state
    uint32_t active_index_id_{0};
    Index* active_index_{nullptr};
    bool index_scan_active_{false};
    std::vector<std::pair<int64_t, RID>> index_scan_results_;
    size_t index_scan_pos_{0};
};

}  // namespace goods_db
