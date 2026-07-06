#include "buffer/buffer_pool_manager.h"
#include <algorithm>
#include "buffer/replacer.h"
#include "common/logger.h"

namespace goods_db {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager* disk_manager,
                                     std::unique_ptr<Replacer> replacer)
    : pool_size_(pool_size),
      disk_manager_(disk_manager),
      replacer_(std::move(replacer)),
      pages_(pool_size) {
    // Initialize free_list_ with all frame IDs
    for (size_t i = 0; i < pool_size_; i++) {
        free_list_.push_back(static_cast<frame_id_t>(i));
    }
}

BufferPoolManager::~BufferPoolManager() {
    FlushAllPages();
}

Page* BufferPoolManager::FetchPage(page_id_t page_id) {
    if (page_id == INVALID_PAGE_ID) return nullptr;

    std::lock_guard<std::mutex> lock(mutex_);
    access_count_++;

    // Check if page is already in pool (page table hit)
    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        frame_id_t frame_id = it->second;
        Page* page = &pages_[frame_id];

        page->Pin();
        replacer_->Pin(frame_id);
        hit_count_++;

        LOG_DEBUG("FetchPage hit: page_id={}, frame_id={}, pin_count={}",
                  page_id, frame_id, page->GetPinCount());
        return page;
    }

    // Cache miss: need to load from disk or allocate
    frame_id_t frame_id = GetFreeFrame();
    if (frame_id == INVALID_FRAME_ID) {
        LOG_ERROR("FetchPage: no free frame available for page_id={}", page_id);
        return nullptr;
    }

    Page* page = &pages_[frame_id];

    // Read page data from disk
    if (!disk_manager_->ReadPage(page_id, page->GetData())) {
        LOG_ERROR("FetchPage: failed to read page_id={} from disk", page_id);
        // Return frame to free list
        free_list_.push_back(frame_id);
        return nullptr;
    }

    // Initialize page metadata
    page->SetPageId(page_id);
    page->Pin();
    page->ClearDirty();
    replacer_->Pin(frame_id);

    // Update page table
    page_table_[page_id] = frame_id;

    LOG_DEBUG("FetchPage miss: page_id={}, frame_id={}, loaded from disk",
              page_id, frame_id);
    return page;
}

bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
    if (page_id == INVALID_PAGE_ID) return false;

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        LOG_WARN("UnpinPage: page_id={} not in buffer pool", page_id);
        return false;
    }

    frame_id_t frame_id = it->second;
    Page* page = &pages_[frame_id];

    if (page->GetPinCount() <= 0) {
        LOG_WARN("UnpinPage: page_id={} already unpinned (pin_count=0)", page_id);
        return false;
    }

    if (is_dirty) {
        page->MarkDirty();
    }

    page->Unpin();

    if (page->GetPinCount() == 0) {
        replacer_->Unpin(frame_id);
    }

    LOG_DEBUG("UnpinPage: page_id={}, pin_count={}, dirty={}",
              page_id, page->GetPinCount(), page->IsDirty());
    return true;
}

Page* BufferPoolManager::NewPage(uint16_t file_id, page_id_t* page_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Allocate a new page on disk
    *page_id = disk_manager_->AllocatePage(file_id);
    if (*page_id == INVALID_PAGE_ID) {
        LOG_ERROR("NewPage: failed to allocate page for file_id={}", file_id);
        return nullptr;
    }

    frame_id_t frame_id = GetFreeFrame();
    if (frame_id == INVALID_FRAME_ID) {
        // Rollback: deallocate the disk page
        disk_manager_->DeallocatePage(*page_id);
        LOG_ERROR("NewPage: no free frame for new page_id={}", *page_id);
        return nullptr;
    }

    Page* page = &pages_[frame_id];

    // Initialize as a fresh page (zeros)
    page->ResetMemory();
    page->SetPageId(*page_id);
    page->Pin();
    page->ClearDirty();

    replacer_->Pin(frame_id);
    page_table_[*page_id] = frame_id;

    LOG_DEBUG("NewPage: allocated page_id={}, frame_id={}", *page_id, frame_id);
    return page;
}

bool BufferPoolManager::DeletePage(page_id_t page_id) {
    if (page_id == INVALID_PAGE_ID) return false;

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        frame_id_t frame_id = it->second;
        Page* page = &pages_[frame_id];

        if (page->GetPinCount() > 0) {
            LOG_WARN("DeletePage: page_id={} is pinned (pin_count={})",
                     page_id, page->GetPinCount());
            return false;
        }

        // Reset the frame
        page->ResetMemory();
        page->SetPageId(INVALID_PAGE_ID);
        page->ClearDirty();

        replacer_->Pin(frame_id);  // Remove from replacer
        page_table_.erase(it);
        free_list_.push_back(frame_id);
    }

    // Deallocate on disk
    disk_manager_->DeallocatePage(page_id);

    LOG_DEBUG("DeletePage: page_id={} deleted", page_id);
    return true;
}

bool BufferPoolManager::FlushPage(page_id_t page_id, bool sync) {
    if (page_id == INVALID_PAGE_ID) return false;

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        LOG_WARN("FlushPage: page_id={} not in buffer pool", page_id);
        return false;  // Not in pool, nothing to flush
    }

    Page* page = &pages_[it->second];
    if (!page->IsDirty()) return true;  // Nothing to flush

    if (!disk_manager_->WritePage(page_id, page->GetData())) {
        LOG_ERROR("FlushPage: failed to write page_id={} to disk", page_id);
        return false;
    }

    page->ClearDirty();

    if (sync) {
        uint16_t file_id = GetFileId(page_id);
        disk_manager_->SyncFile(file_id);
    }

    LOG_DEBUG("FlushPage: page_id={} flushed to disk", page_id);
    return true;
}

void BufferPoolManager::FlushAllPages() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [page_id, frame_id] : page_table_) {
        Page* page = &pages_[frame_id];
        if (page->IsDirty()) {
            disk_manager_->WritePage(page_id, page->GetData());
            page->ClearDirty();
        }
    }

    LOG_INFO("FlushAllPages: all dirty pages flushed");
}

void BufferPoolManager::FlushFilePages(uint16_t file_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [page_id, frame_id] : page_table_) {
        if (GetFileId(page_id) != file_id) continue;

        Page* page = &pages_[frame_id];
        if (page->IsDirty()) {
            disk_manager_->WritePage(page_id, page->GetData());
            page->ClearDirty();
        }
    }
}

double BufferPoolManager::GetHitRate() const {
    size_t total = access_count_.load();
    if (total == 0) return 0.0;
    return static_cast<double>(hit_count_.load()) / total;
}

size_t BufferPoolManager::GetDirtyPageCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (auto& [page_id, frame_id] : page_table_) {
        if (pages_[frame_id].IsDirty()) count++;
    }
    return count;
}

size_t BufferPoolManager::GetFreeFrameCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return free_list_.size();
}

void BufferPoolManager::ResetStats() {
    access_count_ = 0;
    hit_count_ = 0;
}

frame_id_t BufferPoolManager::GetFreeFrame() {
    // First try to use an empty frame from free_list_
    if (!free_list_.empty()) {
        frame_id_t frame_id = free_list_.front();
        free_list_.pop_front();
        return frame_id;
    }

    // No free frames: try to evict one
    return EvictPage();
}

frame_id_t BufferPoolManager::EvictPage() {
    frame_id_t evicted_frame;
    if (!replacer_->Victim(&evicted_frame)) {
        return INVALID_FRAME_ID;  // All pages are pinned
    }

    Page* page = &pages_[evicted_frame];
    page_id_t old_page_id = page->GetPageId();

    // Write dirty page to disk before eviction
    if (page->IsDirty()) {
        if (!disk_manager_->WritePage(old_page_id, page->GetData())) {
            LOG_ERROR("EvictPage: failed to write dirty page_id={}", old_page_id);
            return INVALID_FRAME_ID;
        }
    }

    // Remove from page table
    page_table_.erase(old_page_id);

    // Reset the frame
    page->ResetMemory();
    page->SetPageId(INVALID_PAGE_ID);
    page->ClearDirty();

    LOG_DEBUG("EvictPage: evicted page_id={} from frame_id={}", old_page_id, evicted_frame);
    return evicted_frame;
}

}  // namespace goods_db
