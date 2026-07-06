#include "storage/table/table_heap.h"
#include "buffer/page_guard.h"
#include "common/logger.h"
#include "storage/table/table_iterator.h"

namespace goods_db {

TableHeap::TableHeap(BufferPoolManager* bpm, page_id_t first_page_id)
    : bpm_(bpm), first_page_id_(first_page_id) {
    if (first_page_id != INVALID_PAGE_ID) {
        file_id_ = GetFileId(first_page_id);
    }
}

bool TableHeap::InsertTuple(const Tuple& tuple, RID* rid) {
    uint32_t tuple_size = tuple.GetLength();
    if (tuple_size == 0 || tuple_size > MAX_TUPLE_SIZE) return false;

    // Find a page with enough free space by traversing the page chain
    page_id_t current_page_id = first_page_id_;
    page_id_t prev_page_id = INVALID_PAGE_ID;

    while (current_page_id != INVALID_PAGE_ID) {
        Page* page = bpm_->FetchPage(current_page_id);
        if (!page) return false;

        page->WLatch();
        TablePage table_page;
        table_page.LoadFromData(page->GetData());

        uint32_t free_space = table_page.GetFreeSpace();
        uint32_t needed = tuple_size + SLOT_SIZE;

        if (free_space >= needed) {
            int16_t slot_id;
            bool ok = table_page.InsertTuple(tuple.GetData(), tuple_size, &slot_id);
            if (ok) {
                page->MarkDirty();
                page->WUnlatch();
                bpm_->UnpinPage(current_page_id, true);
                *rid = RID(current_page_id, slot_id);
                return true;
            }
        }

        prev_page_id = current_page_id;
        current_page_id = table_page.GetNextPageId();
        page->WUnlatch();
        bpm_->UnpinPage(current_page_id, false);
    }

    // No page with enough space: allocate a new page
    page_id_t new_page_id = AllocateNewPage(prev_page_id);
    if (new_page_id == INVALID_PAGE_ID) return false;

    Page* new_page = bpm_->FetchPage(new_page_id);
    if (!new_page) return false;

    new_page->WLatch();
    TablePage table_page;
    table_page.LoadFromData(new_page->GetData());
    table_page.Init(new_page_id);

    // Link from previous last page
    if (prev_page_id != INVALID_PAGE_ID) {
        Page* prev_page = bpm_->FetchPage(prev_page_id);
        if (prev_page) {
            prev_page->WLatch();
            TablePage prev_tp;
            prev_tp.LoadFromData(prev_page->GetData());
            prev_tp.SetNextPageId(new_page_id);

            // Set prev_page_id on new page
            table_page.SetPrevPageId(prev_page_id);

            prev_page->MarkDirty();
            prev_page->WUnlatch();
            bpm_->UnpinPage(prev_page_id, true);
        }
    }

    int16_t slot_id;
    bool ok = table_page.InsertTuple(tuple.GetData(), tuple_size, &slot_id);
    GOODS_DB_ASSERT(ok, "InsertTuple into new empty page must succeed");

    new_page->MarkDirty();
    new_page->WUnlatch();
    bpm_->UnpinPage(new_page_id, true);

    *rid = RID(new_page_id, slot_id);
    return true;
}

bool TableHeap::GetTuple(const RID& rid, Tuple* tuple, const Schema* schema) {
    Page* page = bpm_->FetchPage(rid.page_id);
    if (!page) return false;

    page->RLatch();
    TablePage table_page;
    table_page.LoadFromData(page->GetData());

    if (table_page.IsDeleted(rid.slot_id)) {
        page->RUnlatch();
        bpm_->UnpinPage(rid.page_id, false);
        return false;
    }

    char tuple_data[MAX_TUPLE_SIZE];
    uint32_t tuple_size;
    bool ok = table_page.GetTuple(rid.slot_id, tuple_data, &tuple_size);

    page->RUnlatch();
    bpm_->UnpinPage(rid.page_id, false);

    if (!ok) return false;

    *tuple = Tuple::Deserialize(tuple_data, schema);
    return true;
}

bool TableHeap::UpdateTuple(const RID& rid, const Tuple& new_tuple, RID* new_rid) {
    Page* page = bpm_->FetchPage(rid.page_id);
    if (!page) return false;

    page->WLatch();
    TablePage table_page;
    table_page.LoadFromData(page->GetData());

    if (table_page.IsDeleted(rid.slot_id)) {
        page->WUnlatch();
        bpm_->UnpinPage(rid.page_id, false);
        return false;
    }

    uint32_t new_size = new_tuple.GetLength();

    // Try in-place update first
    bool in_place = table_page.UpdateTuple(rid.slot_id, new_tuple.GetData(), new_size);

    page->WUnlatch();
    bpm_->UnpinPage(rid.page_id, in_place);

    if (in_place) {
        *new_rid = rid;
        return true;
    }

    // Tuple grew: mark old as deleted and insert new
    page->WLatch();
    table_page.LoadFromData(page->GetData());
    table_page.MarkDelete(rid.slot_id);
    page->MarkDirty();
    page->WUnlatch();
    bpm_->UnpinPage(rid.page_id, true);

    // Insert new tuple elsewhere
    return InsertTuple(new_tuple, new_rid);
}

bool TableHeap::MarkDelete(const RID& rid) {
    Page* page = bpm_->FetchPage(rid.page_id);
    if (!page) return false;

    page->WLatch();
    TablePage table_page;
    table_page.LoadFromData(page->GetData());

    bool ok = table_page.MarkDelete(rid.slot_id);

    if (ok) page->MarkDirty();
    page->WUnlatch();
    bpm_->UnpinPage(rid.page_id, ok);

    return ok;
}

bool TableHeap::ApplyDelete(const RID& rid) {
    Page* page = bpm_->FetchPage(rid.page_id);
    if (!page) return false;

    page->WLatch();
    TablePage table_page;
    table_page.LoadFromData(page->GetData());

    bool ok = table_page.ApplyDelete(rid.slot_id);

    if (ok) page->MarkDirty();
    page->WUnlatch();
    bpm_->UnpinPage(rid.page_id, ok);

    return ok;
}

std::unique_ptr<TableIterator> TableHeap::MakeIterator() {
    return std::unique_ptr<TableIterator>(
        new TableIterator(bpm_, first_page_id_, nullptr));
}

page_id_t TableHeap::AllocateNewPage(page_id_t prev_page_id) {
    page_id_t new_page_id;
    Page* new_page = bpm_->NewPage(file_id_, &new_page_id);
    if (!new_page) return INVALID_PAGE_ID;

    new_page->WLatch();

    // Initialize as a TablePage
    std::memset(new_page->GetData(), 0, PAGE_SIZE);
    TablePage tp;
    tp.LoadFromData(new_page->GetData());
    tp.Init(new_page_id);
    tp.SetPrevPageId(prev_page_id);

    new_page->MarkDirty();
    new_page->WUnlatch();
    bpm_->UnpinPage(new_page_id, true);

    return new_page_id;
}

page_id_t TableHeap::GetLastPageId() {
    page_id_t current = first_page_id_;
    page_id_t last = current;

    while (current != INVALID_PAGE_ID) {
        last = current;
        Page* page = bpm_->FetchPage(current);
        if (!page) break;

        page->RLatch();
        TablePage tp;
        tp.LoadFromData(page->GetData());
        current = tp.GetNextPageId();
        page->RUnlatch();
        bpm_->UnpinPage(last, false);
    }

    return last;
}

}  // namespace goods_db
