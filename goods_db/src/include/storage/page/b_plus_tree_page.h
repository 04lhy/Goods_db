#pragma once

#include <cstdint>
#include <cstring>
#include "common/config.h"

namespace goods_db {

/**
 * Page type identifiers for B+Tree pages.
 */
enum class IndexPageType : uint8_t {
    INVALID_INDEX_PAGE = 0,
    LEAF_PAGE = 1,
    INTERNAL_PAGE = 2,
};

/**
 * BPlusTreePage - base class for B+Tree internal and leaf pages.
 *
 * Header layout (stored at beginning of page data):
 *   page_type_          (1 byte)  - LEAF or INTERNAL
 *   size_               (2 bytes) - current number of keys
 *   max_size_           (2 bytes) - maximum number of keys
 *   parent_page_id_     (4 bytes) - parent page id in the B+Tree
 *   page_id_            (4 bytes) - this page's id
 *   = 13 bytes header
 */
class BPlusTreePage {
public:
    BPlusTreePage() = default;
    ~BPlusTreePage() = default;

    void Init(IndexPageType type, int32_t max_size) {
        SetPageType(type);
        SetSize(0);
        SetMaxSize(max_size);
        SetParentPageId(INVALID_PAGE_ID);
    }

    // Header accessors (operate on page_data_)
    void SetPageType(IndexPageType type) { data_[0] = static_cast<char>(type); }
    IndexPageType GetPageType() const { return static_cast<IndexPageType>(data_[0]); }

    bool IsLeafPage() const { return GetPageType() == IndexPageType::LEAF_PAGE; }
    bool IsInternalPage() const { return GetPageType() == IndexPageType::INTERNAL_PAGE; }

    void SetSize(int16_t size) { std::memcpy(data_ + 1, &size, sizeof(int16_t)); }
    int16_t GetSize() const {
        int16_t size;
        std::memcpy(&size, data_ + 1, sizeof(int16_t));
        return size;
    }

    void SetMaxSize(int16_t max_size) { std::memcpy(data_ + 3, &max_size, sizeof(int16_t)); }
    int16_t GetMaxSize() const {
        int16_t max_size;
        std::memcpy(&max_size, data_ + 3, sizeof(int16_t));
        return max_size;
    }

    int16_t GetMinSize() const {
        // Minimum occupancy: floor(max_size / 2) for internal, ceil(max_size/2)-1 for leaf
        // Using floor(max_size / 2) as general threshold
        return GetMaxSize() / 2;
    }

    void SetParentPageId(page_id_t parent_id) { std::memcpy(data_ + 5, &parent_id, sizeof(page_id_t)); }
    page_id_t GetParentPageId() const {
        page_id_t pid;
        std::memcpy(&pid, data_ + 5, sizeof(page_id_t));
        return pid;
    }

    void SetPageId(page_id_t page_id) { std::memcpy(data_ + 9, &page_id, sizeof(page_id_t)); }
    page_id_t GetPageId() const {
        page_id_t pid;
        std::memcpy(&pid, data_ + 9, sizeof(page_id_t));
        return pid;
    }

    /** Get number of bytes used by the header */
    static constexpr uint32_t GetHeaderSize() { return 13; }

    /** Set the data pointer (points to the page data in buffer pool) */
    void SetData(char* page_data) { data_ = page_data; }
    char* GetData() { return data_; }
    const char* GetData() const { return data_; }

protected:
    char* data_{nullptr};
};

}  // namespace goods_db
