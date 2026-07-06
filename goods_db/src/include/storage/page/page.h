#pragma once

#include <atomic>
#include <cstring>
#include "common/config.h"
#include "common/rwlatch.h"

namespace goods_db {

/**
 * Page - the in-memory buffer frame that holds a disk page.
 *
 * Each Page represents one PAGE_SIZE (16KB) block in the buffer pool.
 * The Page tracks:
 *   - page_id: which disk page it holds (INVALID_PAGE_ID if empty)
 *   - pin_count: number of active users (0 = evictable)
 *   - is_dirty: whether the in-memory data differs from disk
 *   - data: the actual page content (PAGE_SIZE bytes)
 *   - latch: reader-writer lock for concurrent access
 */
class Page {
public:
    Page() {
        ResetMemory();
    }

    ~Page() = default;

    // Non-copyable, non-movable
    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;

    /** Reset the page memory to zeros */
    inline void ResetMemory() {
        std::memset(data_, 0, PAGE_SIZE);
    }

    /** Get raw data pointer (read-only) */
    inline const char* GetData() const { return data_; }

    /** Get mutable data pointer */
    inline char* GetData() { return data_; }

    /** Get the disk page ID this frame holds */
    inline page_id_t GetPageId() const { return page_id_; }

    /** Set the disk page ID */
    inline void SetPageId(page_id_t page_id) { page_id_ = page_id; }

    /** Get current pin count */
    inline int32_t GetPinCount() const { return pin_count_; }

    /** Increment pin count */
    inline void Pin() { pin_count_++; }

    /** Decrement pin count */
    inline void Unpin() {
        if (pin_count_ > 0) pin_count_--;
    }

    /** Check if page is dirty */
    inline bool IsDirty() const { return is_dirty_; }

    /** Mark page as dirty */
    inline void MarkDirty() { is_dirty_ = true; }

    /** Clear dirty flag */
    inline void ClearDirty() { is_dirty_ = false; }

    /** Get reader-writer latch for this page */
    inline RWLatch& GetLatch() { return latch_; }

    /** Acquire read lock on this page */
    inline void RLatch() { latch_.RLock(); }

    /** Release read lock on this page */
    inline void RUnlatch() { latch_.RUnlock(); }

    /** Acquire write lock on this page */
    inline void WLatch() { latch_.WLock(); }

    /** Release write lock on this page */
    inline void WUnlatch() { latch_.WUnlock(); }

private:
    char data_[PAGE_SIZE];        // page content
    page_id_t page_id_{INVALID_PAGE_ID};  // disk page id
    std::atomic<int32_t> pin_count_{0};   // active user count
    bool is_dirty_{false};                 // dirty flag
    RWLatch latch_;                        // concurrency control
};

}  // namespace goods_db
