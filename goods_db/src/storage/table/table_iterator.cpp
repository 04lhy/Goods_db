#include "storage/table/table_iterator.h"

namespace goods_db {

TableIterator::TableIterator(BufferPoolManager* bpm, page_id_t first_page_id,
                             const Schema* schema)
    : bpm_(bpm), schema_(schema), first_page_id_(first_page_id) {
    Reset();
}

TableIterator::~TableIterator() {
    // Release any page still held by the iterator
    if (current_page_ != nullptr) {
        current_page_->RUnlatch();
        bpm_->UnpinPage(current_page_->GetPageId(), false);
        current_page_ = nullptr;
    }
}

void TableIterator::Reset() {
    current_page_id_ = first_page_id_;
    current_slot_id_ = -1;

    if (current_page_ != nullptr) {
        current_page_->RUnlatch();
        bpm_->UnpinPage(current_page_->GetPageId(), false);
        current_page_ = nullptr;
    }

    AdvanceToNextValid();
}

bool TableIterator::HasNext() {
    return current_page_id_ != INVALID_PAGE_ID;
}

std::pair<RID, Tuple> TableIterator::Next() {
    RID rid(current_page_id_, current_slot_id_);

    // Read the current tuple
    char tuple_data[MAX_TUPLE_SIZE];
    uint32_t tuple_size;
    current_table_page_.GetTuple(current_slot_id_, tuple_data, &tuple_size);

    Tuple tuple = Tuple::Deserialize(tuple_data, schema_);
    tuple.SetRid(rid);

    // Advance to next
    AdvanceToNextValid();

    return {rid, tuple};
}

void TableIterator::AdvanceToNextValid() {
    // If we're currently on a page, try the next slot
    if (current_page_id_ != INVALID_PAGE_ID && current_page_ != nullptr) {
        int16_t slot_count = current_table_page_.GetSlotCount();

        // Try next slot on current page
        for (int16_t i = current_slot_id_ + 1; i < slot_count; i++) {
            if (!current_table_page_.IsDeleted(i)) {
                current_slot_id_ = i;
                return;
            }
        }

        // No more valid tuples on this page → move to next page
        page_id_t next_page_id = current_table_page_.GetNextPageId();

        // Release current page
        current_page_->RUnlatch();
        bpm_->UnpinPage(current_page_->GetPageId(), false);
        current_page_ = nullptr;
        current_page_id_ = next_page_id;
    }

    // Find the next page with valid tuples
    while (current_page_id_ != INVALID_PAGE_ID) {
        current_page_ = bpm_->FetchPage(current_page_id_);
        if (!current_page_) {
            current_page_id_ = INVALID_PAGE_ID;
            return;
        }

        current_page_->RLatch();
        current_table_page_.LoadFromData(current_page_->GetData());

        int16_t slot_count = current_table_page_.GetSlotCount();
        for (int16_t i = 0; i < slot_count; i++) {
            if (!current_table_page_.IsDeleted(i)) {
                current_slot_id_ = i;
                return;
            }
        }

        // All tuples on this page are deleted → go to next page
        page_id_t next_page_id = current_table_page_.GetNextPageId();
        current_page_->RUnlatch();
        bpm_->UnpinPage(current_page_->GetPageId(), false);
        current_page_ = nullptr;
        current_page_id_ = next_page_id;
    }

    // No more pages → end of iteration
    if (current_page_ != nullptr) {
        current_page_->RUnlatch();
        bpm_->UnpinPage(current_page_->GetPageId(), false);
        current_page_ = nullptr;
    }
}

uint32_t TableIterator::Count() {
    uint32_t count = 0;
    Reset();
    while (HasNext()) {
        Next();
        count++;
    }
    // Don't reset — the caller is done; destructor will clean up the page
    return count;
}

}  // namespace goods_db
