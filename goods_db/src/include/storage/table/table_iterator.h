#pragma once

#include "buffer/buffer_pool_manager.h"
#include "common/rid.h"
#include "storage/page/table_page.h"
#include "storage/table/tuple.h"
#include "type/schema.h"

namespace goods_db {

/**
 * TableIterator - sequential iterator over all tuples in a TableHeap.
 *
 * Traverses the doubly-linked page chain, skipping deleted tuples.
 *
 * Usage:
 *   auto it = table_heap.MakeIterator();
 *   while (it->HasNext()) {
 *       auto [rid, tuple] = it->Next();
 *       // process tuple
 *   }
 */
class TableIterator {
public:
    TableIterator(BufferPoolManager* bpm, page_id_t first_page_id,
                  const Schema* schema);

    ~TableIterator();

    /** Check if there are more tuples */
    bool HasNext();

    /** Get the next (RID, Tuple) pair. Advances the iterator. */
    std::pair<RID, Tuple> Next();

    /** Reset iterator to the beginning */
    void Reset();

    /** Get total count of non-deleted tuples (expensive: full scan) */
    uint32_t Count();

private:
    /** Advance to the next valid (non-deleted) slot */
    void AdvanceToNextValid();

    BufferPoolManager* bpm_;
    const Schema* schema_;
    page_id_t first_page_id_;

    page_id_t current_page_id_;
    int16_t current_slot_id_;
    Page* current_page_{nullptr};
    TablePage current_table_page_;
};

}  // namespace goods_db
