#include "storage/page/table_page.h"
#include <algorithm>
#include "common/logger.h"

namespace goods_db {

void TablePage::Init(page_id_t page_id) {
    std::memset(page_data_, 0, PAGE_SIZE);
    SetPageId(page_id);
    SetLSN(0);
    SetPrevPageId(INVALID_PAGE_ID);
    SetNextPageId(INVALID_PAGE_ID);

    uint16_t zero = 0;
    std::memcpy(page_data_ + OFFSET_SLOT_COUNT, &zero, sizeof(uint16_t));
    // free_space_pointer is unused; free space is computed from slot_array_end and lowest_tuple
    std::memcpy(page_data_ + OFFSET_FREE_SPACE_PTR, &zero, sizeof(uint16_t));
}

void TablePage::LoadFromData(char* page_data) {
    page_data_ = page_data;
}

// =============================================================================
// Header accessors
// =============================================================================
page_id_t TablePage::GetPageId() const {
    page_id_t id;
    std::memcpy(&id, page_data_ + OFFSET_PAGE_ID, sizeof(page_id_t));
    return id;
}
void TablePage::SetPageId(page_id_t page_id) {
    std::memcpy(page_data_ + OFFSET_PAGE_ID, &page_id, sizeof(page_id_t));
}
int64_t TablePage::GetLSN() const {
    int64_t lsn;
    std::memcpy(&lsn, page_data_ + OFFSET_LSN, sizeof(int64_t));
    return lsn;
}
void TablePage::SetLSN(int64_t lsn) {
    std::memcpy(page_data_ + OFFSET_LSN, &lsn, sizeof(int64_t));
}
page_id_t TablePage::GetPrevPageId() const {
    page_id_t id;
    std::memcpy(&id, page_data_ + OFFSET_PREV_PAGE_ID, sizeof(page_id_t));
    return id;
}
void TablePage::SetPrevPageId(page_id_t prev) {
    std::memcpy(page_data_ + OFFSET_PREV_PAGE_ID, &prev, sizeof(page_id_t));
}
page_id_t TablePage::GetNextPageId() const {
    page_id_t id;
    std::memcpy(&id, page_data_ + OFFSET_NEXT_PAGE_ID, sizeof(page_id_t));
    return id;
}
void TablePage::SetNextPageId(page_id_t next) {
    std::memcpy(page_data_ + OFFSET_NEXT_PAGE_ID, &next, sizeof(page_id_t));
}
uint16_t TablePage::GetSlotCount() const {
    uint16_t count;
    std::memcpy(&count, page_data_ + OFFSET_SLOT_COUNT, sizeof(uint16_t));
    return count;
}
uint16_t TablePage::GetFreeSpacePointer() const {
    uint16_t fsp;
    std::memcpy(&fsp, page_data_ + OFFSET_FREE_SPACE_PTR, sizeof(uint16_t));
    return fsp;
}

uint32_t TablePage::GetFreeSpace() const {
    // Layout: [Header][Slots...] <-- free space --> [...Tuples]
    // Slots grow toward higher addresses from header.
    // Tuples grow toward lower addresses from PAGE_SIZE.
    uint16_t slot_count = GetSlotCount();
    uint32_t slot_array_end = TABLE_PAGE_HEADER_SIZE +
                              static_cast<uint32_t>(slot_count) * SLOT_SIZE;

    // Find the lowest tuple offset (smallest offset = closest to header)
    uint32_t lowest_tuple = PAGE_SIZE;
    for (int16_t i = 0; i < slot_count; i++) {
        if (IsDeleted(i)) continue;
        uint32_t off, sz;
        GetSlotInfo(i, &off, &sz);
        if (off < lowest_tuple) lowest_tuple = off;
    }

    if (lowest_tuple <= slot_array_end) return 0;
    return lowest_tuple - slot_array_end;
}

bool TablePage::IsInitialized() const {
    return page_data_ != nullptr;
}

// =============================================================================
// Slot operations
// =============================================================================
uint32_t TablePage::GetSlotOffset(int16_t slot_id) const {
    return TABLE_PAGE_HEADER_SIZE + static_cast<uint32_t>(slot_id) * SLOT_SIZE;
}

void TablePage::GetSlotInfo(int16_t slot_id, uint32_t* offset, uint32_t* size) const {
    uint32_t slot_offset = GetSlotOffset(slot_id);
    uint16_t raw_offset, raw_size;
    std::memcpy(&raw_offset, page_data_ + slot_offset, sizeof(uint16_t));
    std::memcpy(&raw_size, page_data_ + slot_offset + 2, sizeof(uint16_t));
    *offset = raw_offset;
    *size = raw_size;
}

void TablePage::SetSlotInfo(int16_t slot_id, uint32_t offset, uint32_t size) {
    uint32_t slot_offset = GetSlotOffset(slot_id);
    uint16_t raw_offset = static_cast<uint16_t>(offset & 0xFFFF);
    uint16_t raw_size = static_cast<uint16_t>(size & 0xFFFF);
    std::memcpy(page_data_ + slot_offset, &raw_offset, sizeof(uint16_t));
    std::memcpy(page_data_ + slot_offset + 2, &raw_size, sizeof(uint16_t));
}

void TablePage::SetSlotDeleted(int16_t slot_id) {
    uint32_t slot_offset = GetSlotOffset(slot_id);
    uint16_t deleted_flag = 0xFFFF;
    std::memcpy(page_data_ + slot_offset, &deleted_flag, sizeof(uint16_t));
}

bool TablePage::IsDeleted(int16_t slot_id) const {
    uint32_t slot_offset = GetSlotOffset(slot_id);
    uint16_t raw_offset;
    std::memcpy(&raw_offset, page_data_ + slot_offset, sizeof(uint16_t));
    return raw_offset == 0xFFFF;
}

// =============================================================================
// Tuple CRUD — Standard Slotted Page Layout
//
//  ┌────────────────────────────────────┐
//  │ Page Header (24 bytes)             │ ← offset 0
//  ├────────────────────────────────────┤
//  │ Slot Array (grows →)               │ ← offset 24, toward higher addr
//  ├────────────────────────────────────┤
//  │           FREE SPACE               │
//  ├────────────────────────────────────┤
//  │ Tuple Data (grows ←)               │ ← from PAGE_SIZE, toward lower addr
//  └────────────────────────────────────┘
// =============================================================================

bool TablePage::InsertTuple(const char* data, uint32_t size, int16_t* slot_id) {
    if (size == 0 || size > MAX_TUPLE_SIZE) return false;

    uint16_t slot_count = GetSlotCount();
    // After insertion we'll have slot_count+1 slots
    uint32_t slot_array_end = TABLE_PAGE_HEADER_SIZE +
                              static_cast<uint32_t>(slot_count + 1) * SLOT_SIZE;

    // Find the lowest tuple offset (closest to header / furthest from PAGE_SIZE)
    uint32_t lowest_tuple = PAGE_SIZE;
    for (int16_t i = 0; i < slot_count; i++) {
        if (IsDeleted(i)) continue;
        uint32_t off, sz;
        GetSlotInfo(i, &off, &sz);
        if (off < lowest_tuple) lowest_tuple = off;
    }

    // New tuple goes at lowest_tuple - size (growing downward from PAGE_SIZE)
    uint32_t new_tuple_offset = lowest_tuple - size;

    // Check if slot array (with new slot) would overlap with tuple data
    if (slot_array_end > new_tuple_offset) return false;

    // Write tuple data
    std::memcpy(page_data_ + new_tuple_offset, data, size);

    // Add slot entry for the new tuple
    *slot_id = slot_count;
    SetSlotInfo(*slot_id, new_tuple_offset, size);

    // Update slot count
    slot_count++;
    std::memcpy(page_data_ + OFFSET_SLOT_COUNT, &slot_count, sizeof(uint16_t));

    return true;
}

bool TablePage::GetTuple(int16_t slot_id, char* data, uint32_t* size) const {
    if (slot_id < 0 || slot_id >= GetSlotCount()) return false;
    if (IsDeleted(slot_id)) return false;

    uint32_t offset, s;
    GetSlotInfo(slot_id, &offset, &s);
    std::memcpy(data, page_data_ + offset, s);
    *size = s;
    return true;
}

bool TablePage::MarkDelete(int16_t slot_id) {
    if (slot_id < 0 || slot_id >= GetSlotCount()) return false;
    if (IsDeleted(slot_id)) return false;
    SetSlotDeleted(slot_id);
    return true;
}

bool TablePage::ApplyDelete(int16_t slot_id) {
    // Hard delete: compact tuples to reclaim space.
    if (slot_id < 0 || slot_id >= GetSlotCount()) return false;
    if (IsDeleted(slot_id)) return false;

    uint16_t slot_count = GetSlotCount();
    uint32_t deleted_offset, deleted_size;
    GetSlotInfo(slot_id, &deleted_offset, &deleted_size);

    // Compact: move tuples that are "above" the deleted one (closer to PAGE_SIZE)
    // downward by deleted_size to close the gap.
    for (int16_t i = 0; i < slot_count; i++) {
        if (i == slot_id || IsDeleted(i)) continue;
        uint32_t off, sz;
        GetSlotInfo(i, &off, &sz);
        if (off > deleted_offset) {
            // This tuple is closer to PAGE_SIZE than the deleted one; move it down
            uint32_t new_off = off - deleted_size;
            std::memmove(page_data_ + new_off, page_data_ + off, sz);
            SetSlotInfo(i, new_off, sz);
        }
    }

    // Remove the slot entry (shift remaining slots)
    for (int16_t i = slot_id; i < slot_count - 1; i++) {
        uint32_t off, sz;
        GetSlotInfo(i + 1, &off, &sz);
        SetSlotInfo(i, off, sz);
    }
    slot_count--;
    std::memcpy(page_data_ + OFFSET_SLOT_COUNT, &slot_count, sizeof(uint16_t));

    return true;
}

bool TablePage::UpdateTuple(int16_t slot_id, const char* new_data,
                             uint32_t new_size) {
    if (slot_id < 0 || slot_id >= GetSlotCount()) return false;
    if (IsDeleted(slot_id)) return false;

    uint32_t old_offset, old_size;
    GetSlotInfo(slot_id, &old_offset, &old_size);

    if (new_size <= old_size) {
        // In-place update (same size or smaller)
        std::memcpy(page_data_ + old_offset, new_data, new_size);
        if (new_size < old_size) {
            std::memset(page_data_ + old_offset + new_size, 0, old_size - new_size);
        }
        SetSlotInfo(slot_id, old_offset, new_size);
        return true;
    }

    // New tuple is larger: mark old as deleted, insert new at end
    MarkDelete(slot_id);
    int16_t new_slot_id;
    if (!InsertTuple(new_data, new_size, &new_slot_id)) {
        // Rollback: restore old slot (undo the MarkDelete)
        // Clear the 0xFFFF marker and restore original offset/size
        SetSlotInfo(slot_id, old_offset, old_size);
        return false;
    }
    return false;  // RID changed (caller should handle)
}

}  // namespace goods_db
