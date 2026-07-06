#pragma once

#include <cstdint>
#include <cstring>
#include "common/config.h"
#include "common/rid.h"

namespace goods_db {

/**
 * TablePage - Slotted Page layout for storing tuples.
 *
 * Page Layout (from low to high address):
 * ┌────────────────────────────────────┐
 * │ Page Header (fixed size)           │ ← page start
 * │   - page_id (4 bytes)              │
 * │   - lsn (8 bytes)                  │
 * │   - prev_page_id (4 bytes)         │
 * │   - next_page_id (4 bytes)         │
 * │   - slot_count (2 bytes)           │
 * │   - free_space_pointer (2 bytes)   │
 * ├────────────────────────────────────┤
 * │   (padding / future use)           │
 * ├────────────────────────────────────┤
 * │ Slot Array: [Slot0, Slot1, ...]    │ ← grows downward (toward higher addr)
 * │   each slot: offset(2B) + size(2B) │
 * ├────────────────────────────────────┤
 * │   ........ free space ........     │ ← between slot array and tuple data
 * ├────────────────────────────────────┤
 * │ Tuple Data Area                    │ ← grows upward (toward lower addr)
 * │   [Tuple N-1, ..., Tuple 1, Tuple 0]│
 * └────────────────────────────────────┘
 */

// Page header size (fixed, at the beginning of each page)
constexpr uint32_t TABLE_PAGE_HEADER_SIZE = 24;

// Offset constants for the header fields
constexpr uint32_t OFFSET_PAGE_ID       = 0;
constexpr uint32_t OFFSET_LSN           = 4;
constexpr uint32_t OFFSET_PREV_PAGE_ID  = 12;
constexpr uint32_t OFFSET_NEXT_PAGE_ID  = 16;
constexpr uint32_t OFFSET_SLOT_COUNT    = 20;
constexpr uint32_t OFFSET_FREE_SPACE_PTR = 22;

// Slot structure: [offset:2B][size:2B]
constexpr uint32_t SLOT_SIZE = 4;

/** Maximum number of slots in a page */
constexpr uint32_t MAX_SLOTS = (PAGE_SIZE - TABLE_PAGE_HEADER_SIZE) / SLOT_SIZE;

class TablePage {
public:
    /** Initialize an empty page */
    void Init(page_id_t page_id = INVALID_PAGE_ID);

    /** Read header fields from raw page data */
    void LoadFromData(char* page_data);

    // =========================================================================
    // Header Accessors
    // =========================================================================

    page_id_t GetPageId() const;
    void SetPageId(page_id_t page_id);

    int64_t GetLSN() const;
    void SetLSN(int64_t lsn);

    page_id_t GetPrevPageId() const;
    void SetPrevPageId(page_id_t prev);

    page_id_t GetNextPageId() const;
    void SetNextPageId(page_id_t next);

    uint16_t GetSlotCount() const;
    uint16_t GetFreeSpacePointer() const;

    /** Get the amount of free space remaining */
    uint32_t GetFreeSpace() const;

    // =========================================================================
    // Tuple CRUD Operations
    // =========================================================================

    /**
     * Insert a tuple into the page.
     * @param data Tuple data bytes
     * @param size Tuple data size
     * @param slot_id [out] The assigned slot id
     * @return true on success, false if not enough space
     */
    bool InsertTuple(const char* data, uint32_t size, int16_t* slot_id);

    /**
     * Get a tuple by slot id.
     * @param slot_id The slot to read
     * @param data [out] Buffer to copy tuple data into
     * @param size [out] Size of the tuple data
     * @return true on success, false if slot is invalid or deleted
     */
    bool GetTuple(int16_t slot_id, char* data, uint32_t* size) const;

    /**
     * Check if a slot is marked as deleted.
     */
    bool IsDeleted(int16_t slot_id) const;

    /**
     * Mark a tuple as deleted (soft delete - space not reclaimed).
     * @param slot_id The slot to mark
     * @return true on success
     */
    bool MarkDelete(int16_t slot_id);

    /**
     * Physically remove a tuple and reclaim space (hard delete).
     * Updates slot array and tuple data area.
     * @param slot_id The slot to remove
     * @return true on success
     */
    bool ApplyDelete(int16_t slot_id);

    /**
     * Update a tuple in-place.
     * @param slot_id The slot to update
     * @param new_data New tuple data
     * @param new_size New tuple size
     * @return true if updated in-place, false if tuple grew and needs re-insert
     */
    bool UpdateTuple(int16_t slot_id, const char* new_data, uint32_t new_size);

    /**
     * Get total count of all slots (including deleted ones).
     */
    uint16_t GetTupleCount() const { return GetSlotCount(); }

    /** Check if the page has been initialized */
    bool IsInitialized() const;

private:
    /** Get the offset within the page where a slot entry is stored */
    uint32_t GetSlotOffset(int16_t slot_id) const;

    /** Get the data area pointer from a slot */
    void GetSlotInfo(int16_t slot_id, uint32_t* offset, uint32_t* size) const;

    /** Set slot info (offset + size) */
    void SetSlotInfo(int16_t slot_id, uint32_t offset, uint32_t size);

    /** Mark a slot as deleted (set offset to 0xFFFF) */
    void SetSlotDeleted(int16_t slot_id);

    char* page_data_{nullptr};  // pointer to the page data in the buffer pool
};

}  // namespace goods_db
