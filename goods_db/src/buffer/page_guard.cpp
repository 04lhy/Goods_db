#include "buffer/page_guard.h"

namespace goods_db {

// =========================================================================
// ReadPageGuard
// =========================================================================

ReadPageGuard::ReadPageGuard(BufferPoolManager* bpm, Page* page)
    : bpm_(bpm), page_(page) {
    if (page_) {
        page_->RLatch();
    }
}

ReadPageGuard::~ReadPageGuard() {
    Drop();
}

ReadPageGuard::ReadPageGuard(ReadPageGuard&& other) noexcept
    : bpm_(other.bpm_), page_(other.page_) {
    other.bpm_ = nullptr;
    other.page_ = nullptr;
}

ReadPageGuard& ReadPageGuard::operator=(ReadPageGuard&& other) noexcept {
    if (this != &other) {
        Drop();
        bpm_ = other.bpm_;
        page_ = other.page_;
        other.bpm_ = nullptr;
        other.page_ = nullptr;
    }
    return *this;
}

void ReadPageGuard::Drop() {
    if (page_ != nullptr) {
        page_->RUnlatch();
        bpm_->UnpinPage(page_->GetPageId(), false);
        page_ = nullptr;
        bpm_ = nullptr;
    }
}

// =========================================================================
// WritePageGuard
// =========================================================================

WritePageGuard::WritePageGuard(BufferPoolManager* bpm, Page* page)
    : bpm_(bpm), page_(page) {
    if (page_) {
        page_->WLatch();
    }
}

WritePageGuard::~WritePageGuard() {
    Drop();
}

WritePageGuard::WritePageGuard(WritePageGuard&& other) noexcept
    : bpm_(other.bpm_), page_(other.page_) {
    other.bpm_ = nullptr;
    other.page_ = nullptr;
}

WritePageGuard& WritePageGuard::operator=(WritePageGuard&& other) noexcept {
    if (this != &other) {
        Drop();
        bpm_ = other.bpm_;
        page_ = other.page_;
        other.bpm_ = nullptr;
        other.page_ = nullptr;
    }
    return *this;
}

void WritePageGuard::Drop() {
    if (page_ != nullptr) {
        page_->WUnlatch();
        bpm_->UnpinPage(page_->GetPageId(), page_->IsDirty());
        page_ = nullptr;
        bpm_ = nullptr;
    }
}

}  // namespace goods_db
