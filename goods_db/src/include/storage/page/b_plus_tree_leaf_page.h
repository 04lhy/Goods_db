#pragma once

#include <cstdint>
#include <cstring>
#include "common/rid.h"
#include "storage/page/b_plus_tree_page.h"

namespace goods_db {

/**
 * B+Tree Leaf Page layout (after BPlusTreePage header):
 *
 * Header extension:
 *   next_page_id_  (4 bytes) - sibling leaf page for range scans
 *
 * Key-Value pairs [key₀, rid₀, key₁, rid₁, ..., key_{n-1}, rid_{n-1}]
 *   - key: 8 bytes (int64_t)
 *   - rid:  6 bytes (page_id 4B + slot_id 2B)
 *   - Each pair: 14 bytes
 *
 * After the base header (13 bytes) + next_page_id (4 bytes) = 17 bytes
 * Tuple data starts at offset 17.
 */
class BPlusTreeLeafPage : public BPlusTreePage {
public:
    static constexpr uint32_t LEAF_HEADER_SIZE = BPlusTreePage::GetHeaderSize() + 4;  // +4 for next_page_id
    static constexpr uint32_t KEY_SIZE = 8;
    static constexpr uint32_t RID_SIZE = 6;
    static constexpr uint32_t KV_SIZE = KEY_SIZE + RID_SIZE;

    void Init(int32_t max_size) {
        BPlusTreePage::Init(IndexPageType::LEAF_PAGE, max_size);
        SetNextPageId(INVALID_PAGE_ID);
    }

    // next_page_id for sibling leaf list (range scan)
    void SetNextPageId(page_id_t next_id) {
        std::memcpy(data_ + BPlusTreePage::GetHeaderSize(), &next_id, sizeof(page_id_t));
    }
    page_id_t GetNextPageId() const {
        page_id_t pid;
        std::memcpy(&pid, data_ + BPlusTreePage::GetHeaderSize(), sizeof(page_id_t));
        return pid;
    }

    // Key access
    int64_t KeyAt(int16_t index) const {
        int64_t key;
        std::memcpy(&key, data_ + LEAF_HEADER_SIZE + index * KV_SIZE, KEY_SIZE);
        return key;
    }

    void SetKeyAt(int16_t index, int64_t key) {
        std::memcpy(data_ + LEAF_HEADER_SIZE + index * KV_SIZE, &key, KEY_SIZE);
    }

    // RID access
    RID ValueAt(int16_t index) const {
        RID rid;
        std::memcpy(&rid.page_id, data_ + LEAF_HEADER_SIZE + index * KV_SIZE + KEY_SIZE, sizeof(int32_t));
        std::memcpy(&rid.slot_id, data_ + LEAF_HEADER_SIZE + index * KV_SIZE + KEY_SIZE + sizeof(int32_t), sizeof(int16_t));
        return rid;
    }

    void SetValueAt(int16_t index, const RID& rid) {
        std::memcpy(data_ + LEAF_HEADER_SIZE + index * KV_SIZE + KEY_SIZE, &rid.page_id, sizeof(int32_t));
        std::memcpy(data_ + LEAF_HEADER_SIZE + index * KV_SIZE + KEY_SIZE + sizeof(int32_t), &rid.slot_id, sizeof(int16_t));
    }

    /**
     * Lookup a key in the leaf page (binary search).
     * Returns the index if found, or -1 if not found.
     */
    int16_t Lookup(int64_t key) const {
        int16_t left = 0, right = GetSize() - 1;
        while (left <= right) {
            int16_t mid = left + (right - left) / 2;
            int64_t mid_key = KeyAt(mid);
            if (mid_key == key) return mid;
            if (key < mid_key) right = mid - 1;
            else left = mid + 1;
        }
        return -1;
    }

    /**
     * Find the index of the first key >= the given key (for range scan start).
     */
    int16_t LowerBound(int64_t key) const {
        int16_t left = 0, right = GetSize();
        while (left < right) {
            int16_t mid = left + (right - left) / 2;
            if (KeyAt(mid) < key) left = mid + 1;
            else right = mid;
        }
        return left;
    }

    /**
     * Insert a KV pair into the leaf page (maintaining sorted order).
     * @return true on success, false if leaf is full
     */
    bool Insert(int64_t key, const RID& rid) {
        int16_t size = GetSize();
        if (size >= GetMaxSize()) return false;

        // Find insertion point
        int16_t insert_pos = size;
        for (int16_t i = 0; i < size; i++) {
            if (key < KeyAt(i)) {
                insert_pos = i;
                break;
            }
        }

        // Shift elements to make space
        for (int16_t i = size - 1; i >= insert_pos; i--) {
            SetKeyAt(i + 1, KeyAt(i));
            SetValueAt(i + 1, ValueAt(i));
        }

        SetKeyAt(insert_pos, key);
        SetValueAt(insert_pos, rid);
        SetSize(size + 1);
        return true;
    }

    /**
     * Remove a KV pair by key.
     * @return true if key was found and removed
     */
    bool Remove(int64_t key) {
        int16_t pos = Lookup(key);
        if (pos < 0) return false;

        int16_t size = GetSize();
        for (int16_t i = pos; i < size - 1; i++) {
            SetKeyAt(i, KeyAt(i + 1));
            SetValueAt(i, ValueAt(i + 1));
        }
        SetSize(size - 1);
        return true;
    }

    /** Move half the entries to a recipient leaf page */
    void MoveHalfTo(BPlusTreeLeafPage* recipient) {
        int16_t size = GetSize();
        int16_t split_point = size / 2;
        int16_t recipient_start = recipient->GetSize();

        for (int16_t i = split_point; i < size; i++) {
            recipient->SetKeyAt(recipient_start + (i - split_point), KeyAt(i));
            recipient->SetValueAt(recipient_start + (i - split_point), ValueAt(i));
        }

        recipient->SetSize(recipient_start + (size - split_point));
        SetSize(split_point);

        // Maintain the leaf linked list
        recipient->SetNextPageId(GetNextPageId());
        SetNextPageId(recipient->GetPageId());
    }

    /** Get the first key in this leaf (used to update parent after split) */
    int64_t GetFirstKey() const {
        if (GetSize() == 0) return 0;
        return KeyAt(0);
    }

    static int32_t GetMaxKeys() { return (PAGE_SIZE - LEAF_HEADER_SIZE) / KV_SIZE; }
};

}  // namespace goods_db
