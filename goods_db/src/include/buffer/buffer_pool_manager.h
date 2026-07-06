#pragma once

#include <atomic>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "common/config.h"
#include "storage/disk/disk_manager.h"
#include "storage/page/page.h"

namespace goods_db {

class Replacer;

/**
 * BufferPoolManager: manages the in-memory buffer pool of pages.
 *
 * The central component of the storage engine. It:
 * 1. Fetches pages from DiskManager on demand (cache miss)
 * 2. Maintains a page table (page_id → frame_id)
 * 3. Uses a Replacer to choose victim frames when the pool is full
 * 4. Flushes dirty pages back to disk before eviction
 *
 * Thread-safe: all public methods are protected by a global mutex.
 */
class BufferPoolManager {
public:
    /**
     * @param pool_size Number of frames in the buffer pool
     * @param disk_manager The disk manager for I/O
     * @param replacer The page replacement policy
     */
    BufferPoolManager(size_t pool_size, DiskManager* disk_manager,
                      std::unique_ptr<Replacer> replacer);

    ~BufferPoolManager();

    // Non-copyable, non-movable
    BufferPoolManager(const BufferPoolManager&) = delete;
    BufferPoolManager& operator=(const BufferPoolManager&) = delete;

    // =========================================================================
    // Core Page Operations
    // =========================================================================

    /**
     * Fetch a page from the buffer pool.
     * Increments pin_count. If page not in pool, reads from disk.
     * If pool is full, evicts a page using the replacer.
     *
     * @param page_id The disk page to fetch
     * @return Pointer to the Page, or nullptr on error
     */
    Page* FetchPage(page_id_t page_id);

    /**
     * Unpin a page. Decrements pin_count.
     * When pin_count reaches 0, the page becomes evictable.
     *
     * @param page_id The page to unpin
     * @param is_dirty Set to true if page was modified
     * @return true on success
     */
    bool UnpinPage(page_id_t page_id, bool is_dirty);

    /**
     * Allocate a new page on disk and load it into the buffer pool.
     *
     * @param file_id The file to allocate in
     * @param page_id [out] The new page's ID
     * @return Pointer to the new Page, or nullptr on error
     */
    Page* NewPage(uint16_t file_id, page_id_t* page_id);

    /**
     * Delete a page from disk and remove it from the buffer pool.
     *
     * @param page_id The page to delete
     * @return true on success
     */
    bool DeletePage(page_id_t page_id);

    // =========================================================================
    // Flush Operations
    // =========================================================================

    /**
     * Flush a specific page to disk (if dirty).
     * Does NOT unpin the page.
     *
     * @param page_id The page to flush
     * @param sync If true, also fsync the file
     * @return true on success
     */
    bool FlushPage(page_id_t page_id, bool sync = false);

    /**
     * Flush all dirty pages to disk.
     * Does NOT unpin pages.
     */
    void FlushAllPages();

    /**
     * Flush all dirty pages for a specific file.
     */
    void FlushFilePages(uint16_t file_id);

    // =========================================================================
    // Statistics
    // =========================================================================

    /** Get buffer pool hit rate (hits / total accesses) */
    double GetHitRate() const;

    /** Get number of dirty pages currently in pool */
    size_t GetDirtyPageCount() const;

    /** Get number of free frames */
    size_t GetFreeFrameCount() const;

    /** Get total number of frames */
    size_t GetPoolSize() const { return pool_size_; }

    /** Reset statistics counters */
    void ResetStats();

private:
    /** Find a free frame or evict one. Returns INVALID_FRAME_ID on failure. */
    frame_id_t GetFreeFrame();

    /** Evict a page using the replacer. Returns frame_id or INVALID_FRAME_ID. */
    frame_id_t EvictPage();

    /** Write page to disk if dirty, then reset frame */
    void FlushAndResetFrame(frame_id_t frame_id);

    size_t pool_size_;
    DiskManager* disk_manager_;
    std::unique_ptr<Replacer> replacer_;

    /** Array of Pages that form the buffer pool */
    std::vector<Page> pages_;

    /** Page table: page_id → frame_id */
    std::unordered_map<page_id_t, frame_id_t> page_table_;

    /** List of free (unused) frame IDs */
    std::list<frame_id_t> free_list_;

    /** Statistics */
    std::atomic<size_t> access_count_{0};
    std::atomic<size_t> hit_count_{0};

    /** Global mutex for thread safety */
    mutable std::mutex mutex_;
};

}  // namespace goods_db
