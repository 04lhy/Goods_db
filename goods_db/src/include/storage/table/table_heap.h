#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include "buffer/buffer_pool_manager.h"
#include "storage/page/table_page.h"
#include "storage/table/tuple.h"
#include "type/schema.h"

namespace goods_db {

class TableIterator;

/**
 * TableHeap - manages the storage of tuples in a table.
 *
 * Uses a doubly-linked list of TablePages.
 * Each TablePage is a slotted page that stores multiple tuples.
 *
 * The table is identified by file_id in the DiskManager.
 * TableHeap uses BufferPoolManager for all page accesses.
 */
class TableHeap {
public:
    /**
     * @param buffer_pool_manager The buffer pool
     * @param first_page_id The page_id of the first page in the table's page chain
     */
    TableHeap(BufferPoolManager* bpm, page_id_t first_page_id);

    ~TableHeap() = default;

    // =========================================================================
    // Tuple CRUD
    // =========================================================================

    /**
     * Insert a tuple into the table.
     * Scans page chain for a page with free space, or allocates a new page.
     * @param tuple The tuple to insert
     * @param rid [out] The RID assigned to the inserted tuple
     * @return true on success
     */
    bool InsertTuple(const Tuple& tuple, RID* rid);

    /**
     * Get a tuple by its RID.
     * @return true if the tuple exists and is not deleted
     */
    bool GetTuple(const RID& rid, Tuple* tuple, const Schema* schema);

    /**
     * Update a tuple.
     * If the new tuple fits in-place, updates directly.
     * Otherwise marks old tuple as deleted and inserts new tuple.
     * @param rid The RID of the tuple to update
     * @param new_tuple The new tuple data
     * @param new_rid [out] New RID if re-inserted; same as old RID if in-place
     * @return true on success
     */
    bool UpdateTuple(const RID& rid, const Tuple& new_tuple, RID* new_rid);

    /**
     * Mark a tuple as deleted (soft delete).
     * Space is not reclaimed until ApplyDelete is called.
     */
    bool MarkDelete(const RID& rid);

    /**
     * Physically remove a tuple and reclaim space.
     */
    bool ApplyDelete(const RID& rid);

    // =========================================================================
    // Iteration
    // =========================================================================

    /** Create an iterator for full table scan */
    std::unique_ptr<TableIterator> MakeIterator();

    /** Get the total number of pages (including empty ones) */
    page_id_t GetFirstPageId() const { return first_page_id_; }

    /** Get the buffer pool manager */
    BufferPoolManager* GetBufferPoolManager() const { return bpm_; }

private:
    /**
     * Allocate a new page and append it to the page chain.
     * @return The new page's page_id, or INVALID_PAGE_ID on failure
     */
    page_id_t AllocateNewPage(page_id_t prev_page_id);

    /**
     * Get the page_id of the last page in the chain.
     */
    page_id_t GetLastPageId();

    BufferPoolManager* bpm_;
    page_id_t first_page_id_;
    uint16_t file_id_;
};

}  // namespace goods_db
