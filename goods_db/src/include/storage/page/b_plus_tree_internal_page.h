#pragma once

#include <cstdint>
#include <cstring>
#include "storage/page/b_plus_tree_page.h"

namespace goods_db {

/**
 * B+Tree Internal Page layout (after header):
 *
 *  [key₀, value₀, key₁, value₁, ..., key_{n-1}, value_{n-1}]
 *
 * - keys are 8 bytes (int64_t by default)
 * - values are 4 bytes (page_id_t, pointing to child pages)
 * - Each KV pair takes 12 bytes
 * - key₀ is always the "separator key" for the subtree pointed by value₀
 *
 * So the first entry has: key=separator, value=left_child_page_id
 * For internal node with N keys, there are N+1 child pointers.
 * Layout: [key₀|child₀|key₁|child₁|...|key_{size-1}|child_{size-1}]
 * But we store the first child separately...
 *
 * Simplified layout:
 *   [child₀₀| key₀ | child₀₁ | key₁ | child₀₂ | ... | key_{n-1} | child₀ₙ]
 *
 * Actually, let's use a simpler approach:
 *   [key₀, child_page_id₀, key₁, child_page_id₁, ..., key_{n-1}, child_page_id_{n-1}]
 *
 * Where child_page_id_i is the page id of the subtree containing keys <= key_i
 * and child_page_id_{n-1} is the rightmost child (keys > key_{n-1}).
 *
 * For lookup:
 *   - If search_key <= key₀: go to child₀
 *   - If key_{i-1} < search_key <= key_i: go to child_i
 *   - If search_key > key_{n-1}: go to child_{n-1} (the rightmost child)
 */
class BPlusTreeInternalPage : public BPlusTreePage {
public:
    static constexpr uint32_t INTERNAL_HEADER_SIZE = BPlusTreePage::GetHeaderSize();
    static constexpr uint32_t KEY_SIZE = 8;        // int64_t
    static constexpr uint32_t VALUE_SIZE = 4;       // page_id_t
    static constexpr uint32_t KV_SIZE = KEY_SIZE + VALUE_SIZE;

    void Init(int32_t max_size) {
        BPlusTreePage::Init(IndexPageType::INTERNAL_PAGE, max_size);
    }

    // Key-Value pair access
    int64_t KeyAt(int16_t index) const {
        int64_t key;
        std::memcpy(&key, data_ + INTERNAL_HEADER_SIZE + index * KV_SIZE, KEY_SIZE);
        return key;
    }

    void SetKeyAt(int16_t index, int64_t key) {
        std::memcpy(data_ + INTERNAL_HEADER_SIZE + index * KV_SIZE, &key, KEY_SIZE);
    }

    page_id_t ValueAt(int16_t index) const {
        page_id_t value;
        std::memcpy(&value, data_ + INTERNAL_HEADER_SIZE + index * KV_SIZE + KEY_SIZE, VALUE_SIZE);
        return value;
    }

    void SetValueAt(int16_t index, page_id_t value) {
        std::memcpy(data_ + INTERNAL_HEADER_SIZE + index * KV_SIZE + KEY_SIZE, &value, VALUE_SIZE);
    }

    /**
     * Lookup: find the child page id for a given search key.
     *
     * Returns the page_id of the child subtree that should contain the key.
     */
    page_id_t Lookup(int64_t key) const {
        int16_t size = GetSize();
        if (size == 0) return INVALID_PAGE_ID;

        // Binary search for the first key >= search_key
        int16_t left = 0, right = size - 1;
        while (left <= right) {
            int16_t mid = left + (right - left) / 2;
            if (key <= KeyAt(mid)) {
                right = mid - 1;
            } else {
                left = mid + 1;
            }
        }

        // 'left' is the index of the first key >= search_key
        // The child at that index should contain the key
        if (static_cast<int16_t>(size) <= left) {
            // key is greater than all separator keys → rightmost child
            return ValueAt(size - 1);
        }
        return ValueAt(left);
    }

    /**
     * Insert a KV pair into the internal page.
     * Assumes space is available.
     * @param key The separator key
     * @param value The child page_id
     * @return true on success
     */
    bool Insert(int64_t key, page_id_t value) {
        int16_t size = GetSize();
        if (size >= GetMaxSize()) return false;

        // Find insertion point (maintain sorted order by key)
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
        SetValueAt(insert_pos, value);
        SetSize(size + 1);
        return true;
    }

    /**
     * Remove a KV pair by key.
     * @return true if key was found and removed
     */
    bool Remove(int64_t key) {
        int16_t size = GetSize();
        int16_t remove_pos = -1;
        for (int16_t i = 0; i < size; i++) {
            if (KeyAt(i) == key) {
                remove_pos = i;
                break;
            }
        }
        if (remove_pos < 0) return false;

        // Shift elements to close the gap
        for (int16_t i = remove_pos; i < size - 1; i++) {
            SetKeyAt(i, KeyAt(i + 1));
            SetValueAt(i, ValueAt(i + 1));
        }
        SetSize(size - 1);
        return true;
    }

    /** Move half the entries to a new (recipient) internal page */
    void MoveHalfTo(BPlusTreeInternalPage* recipient) {
        int16_t size = GetSize();
        int16_t split_point = size / 2;
        int16_t recipient_start = recipient->GetSize();

        for (int16_t i = split_point; i < size; i++) {
            recipient->SetKeyAt(recipient_start + (i - split_point), KeyAt(i));
            recipient->SetValueAt(recipient_start + (i - split_point), ValueAt(i));
        }

        recipient->SetSize(recipient_start + (size - split_point));
        SetSize(split_point);
    }

    /** Copy the last entry to a parent internal page (used during split) */
    void CopyLastTo(BPlusTreeInternalPage* parent, page_id_t child_page_id) {
        int16_t size = GetSize();
        int64_t last_key = KeyAt(size - 1);
        parent->Insert(last_key, child_page_id);
    }

    static int32_t GetMaxKeys() { return (PAGE_SIZE - INTERNAL_HEADER_SIZE) / KV_SIZE; }
};

}  // namespace goods_db
