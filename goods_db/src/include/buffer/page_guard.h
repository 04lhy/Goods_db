#pragma once

#include "buffer/buffer_pool_manager.h"
#include "storage/page/page.h"

namespace goods_db {

/**
 * PageGuard - RAII wrapper for Page access.
 *
 * Automatically handles pinning/latching on construction and
 * unlatching/unpinning on destruction.
 *
 * ReadPageGuard acquires RLock; WritePageGuard acquires WLock.
 *
 * Usage:
 *   auto guard = bpm.FetchPageRead(page_id);
 *   const char* data = guard.GetData();
 *   // ... read data ...
 *   // guard destructor auto-unlocks and unpins
 */
class ReadPageGuard {
public:
    ReadPageGuard() = default;
    ReadPageGuard(BufferPoolManager* bpm, Page* page);
    ~ReadPageGuard();

    // Movable, non-copyable
    ReadPageGuard(ReadPageGuard&& other) noexcept;
    ReadPageGuard& operator=(ReadPageGuard&& other) noexcept;
    ReadPageGuard(const ReadPageGuard&) = delete;
    ReadPageGuard& operator=(const ReadPageGuard&) = delete;

    /** Get read-only page data */
    const char* GetData() const { return page_->GetData(); }

    /** Get page ID */
    page_id_t GetPageId() const { return page_->GetPageId(); }

    /** Release the guard early */
    void Drop();

    explicit operator bool() const { return page_ != nullptr; }

private:
    BufferPoolManager* bpm_{nullptr};
    Page* page_{nullptr};
};

class WritePageGuard {
public:
    WritePageGuard() = default;
    WritePageGuard(BufferPoolManager* bpm, Page* page);
    ~WritePageGuard();

    // Movable, non-copyable
    WritePageGuard(WritePageGuard&& other) noexcept;
    WritePageGuard& operator=(WritePageGuard&& other) noexcept;
    WritePageGuard(const WritePageGuard&) = delete;
    WritePageGuard& operator=(const WritePageGuard&) = delete;

    /** Get mutable page data */
    char* GetData() { return page_->GetData(); }

    /** Get read-only page data */
    const char* GetData() const { return page_->GetData(); }

    /** Get page ID */
    page_id_t GetPageId() const { return page_->GetPageId(); }

    /** Mark page as dirty */
    void MarkDirty() { page_->MarkDirty(); }

    /** Release the guard early */
    void Drop();

    explicit operator bool() const { return page_ != nullptr; }

private:
    BufferPoolManager* bpm_{nullptr};
    Page* page_{nullptr};
};

}  // namespace goods_db
