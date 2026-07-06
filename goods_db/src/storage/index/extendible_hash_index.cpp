#include "storage/index/extendible_hash_index.h"
#include <cstring>
#include "common/logger.h"

namespace goods_db {

ExtendibleHashIndex::ExtendibleHashIndex(BufferPoolManager* bpm,
                                         page_id_t directory_page_id)
    : bpm_(bpm), directory_page_id_(directory_page_id) {}

uint32_t ExtendibleHashIndex::HashKey(int64_t key) const {
    // Simple hash: use murmur3-like mixing
    uint64_t k = static_cast<uint64_t>(key);
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return static_cast<uint32_t>(k);
}

page_id_t ExtendibleHashIndex::GetBucketPageId(int64_t key) {
    if (directory_page_id_ == INVALID_PAGE_ID) return INVALID_PAGE_ID;

    Page* dir_page = bpm_->FetchPage(directory_page_id_);
    if (!dir_page) return INVALID_PAGE_ID;

    char* data = dir_page->GetData();
    uint32_t global_depth;
    std::memcpy(&global_depth, data, 4);

    uint32_t dir_index = HashKey(key) & ((1 << global_depth) - 1);

    // Directory entry layout: [global_depth(4B)] [local_depth0(4B)][bucket_id0(4B)] ...
    uint32_t entry_offset = 4 + dir_index * 8;
    uint32_t bucket_id;
    std::memcpy(&bucket_id, data + entry_offset + 4, 4);

    bpm_->UnpinPage(directory_page_id_, false);
    return static_cast<page_id_t>(bucket_id);
}

bool ExtendibleHashIndex::Insert(int64_t key, const RID& rid) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Initialize directory on first insert
    if (directory_page_id_ == INVALID_PAGE_ID) {
        // Create directory page with global_depth=1 (2 entries)
        page_id_t dir_id;
        Page* dir_page = bpm_->NewPage(0, &dir_id);
        if (!dir_page) return false;

        char* dir_data = dir_page->GetData();
        std::memset(dir_data, 0, PAGE_SIZE);

        uint32_t global_depth = 1;
        std::memcpy(dir_data, &global_depth, 4);

        // Create two bucket pages
        for (uint32_t i = 0; i < 2; i++) {
            page_id_t bucket_id;
            Page* bucket_page = bpm_->NewPage(0, &bucket_id);
            if (!bucket_page) {
                bpm_->UnpinPage(dir_id, false);
                return false;
            }

            char* bucket_data = bucket_page->GetData();
            std::memset(bucket_data, 0, PAGE_SIZE);
            uint32_t local_depth = 1;
            std::memcpy(bucket_data, &local_depth, 4);
            uint16_t zero_size = 0;
            std::memcpy(bucket_data + 4, &zero_size, 2);

            bucket_page->MarkDirty();
            bpm_->UnpinPage(bucket_id, true);

            // Set directory entry
            uint32_t entry_offset = 4 + i * 8;
            std::memcpy(dir_data + entry_offset, &local_depth, 4);
            std::memcpy(dir_data + entry_offset + 4, &bucket_id, 4);
        }

        dir_page->MarkDirty();
        bpm_->UnpinPage(dir_id, true);
        directory_page_id_ = dir_id;
    }

    // Get target bucket
    page_id_t bucket_id = GetBucketPageId(key);
    if (bucket_id == INVALID_PAGE_ID) return false;

    if (!InsertIntoBucket(bucket_id, key, rid)) {
        // Bucket full: split it
        SplitBucket(bucket_id);
        // Retry
        bucket_id = GetBucketPageId(key);
        if (!InsertIntoBucket(bucket_id, key, rid)) {
            return false;
        }
    }

    size_++;
    return true;
}

bool ExtendibleHashIndex::InsertIntoBucket(page_id_t bucket_page_id,
                                            int64_t key, const RID& rid) {
    Page* page = bpm_->FetchPage(bucket_page_id);
    if (!page) return false;

    char* data = page->GetData();
    uint32_t local_depth;
    std::memcpy(&local_depth, data, 4);
    uint16_t bucket_size;
    std::memcpy(&bucket_size, data + 4, 2);

    if (bucket_size >= MAX_BUCKET_SIZE) {
        bpm_->UnpinPage(bucket_page_id, false);
        return false;  // Bucket is full
    }

    // Append KV pair
    uint32_t kv_offset = BUCKET_HEADER_SIZE + bucket_size * BUCKET_KV_SIZE;
    std::memcpy(data + kv_offset, &key, 8);
    std::memcpy(data + kv_offset + 8, &rid.page_id, 4);
    std::memcpy(data + kv_offset + 12, &rid.slot_id, 2);

    bucket_size++;
    std::memcpy(data + 4, &bucket_size, 2);

    page->MarkDirty();
    bpm_->UnpinPage(bucket_page_id, true);
    return true;
}

void ExtendibleHashIndex::SplitBucket(page_id_t bucket_page_id) {
    Page* bucket_page = bpm_->FetchPage(bucket_page_id);
    if (!bucket_page) return;

    char* bucket_data = bucket_page->GetData();
    uint32_t local_depth;
    std::memcpy(&local_depth, bucket_data, 4);
    uint16_t bucket_size;
    std::memcpy(&bucket_size, bucket_data + 4, 2);

    // Read all KV pairs from the old bucket
    std::vector<std::pair<int64_t, RID>> entries;
    for (uint16_t i = 0; i < bucket_size; i++) {
        uint32_t offset = BUCKET_HEADER_SIZE + i * BUCKET_KV_SIZE;
        int64_t k;
        RID r;
        std::memcpy(&k, bucket_data + offset, 8);
        std::memcpy(&r.page_id, bucket_data + offset + 8, 4);
        std::memcpy(&r.slot_id, bucket_data + offset + 12, 2);
        entries.push_back({k, r});
    }

    // Read global depth from directory
    Page* dir_page = bpm_->FetchPage(directory_page_id_);
    if (!dir_page) {
        bpm_->UnpinPage(bucket_page_id, false);
        return;
    }

    char* dir_data = dir_page->GetData();
    uint32_t global_depth;
    std::memcpy(&global_depth, dir_data, 4);

    if (local_depth < global_depth) {
        // Create a new bucket (sibling)
        page_id_t new_bucket_id;
        Page* new_bucket_page = bpm_->NewPage(0, &new_bucket_id);
        if (!new_bucket_page) {
            bpm_->UnpinPage(bucket_page_id, false);
            bpm_->UnpinPage(directory_page_id_, false);
            return;
        }

        // Initialize new bucket with local_depth + 1
        char* new_data = new_bucket_page->GetData();
        std::memset(new_data, 0, PAGE_SIZE);
        uint32_t new_local_depth = local_depth + 1;
        std::memcpy(new_data, &new_local_depth, 4);
        uint16_t zero = 0;
        std::memcpy(new_data + 4, &zero, 2);

        // Update directory entries that should point to the new bucket
        uint32_t mask = 1 << local_depth;
        for (uint32_t i = 0; i < (1u << global_depth); i++) {
            uint32_t entry_offset = 4 + i * 8;
            uint32_t old_bucket;
            std::memcpy(&old_bucket, dir_data + entry_offset + 4, 4);

            if (static_cast<page_id_t>(old_bucket) == bucket_page_id) {
                // This entry pointed to the old bucket
                if (i & mask) {
                    // Point to the new bucket instead
                    std::memcpy(dir_data + entry_offset, &new_local_depth, 4);
                    std::memcpy(dir_data + entry_offset + 4, &new_bucket_id, 4);
                } else {
                    // Update local depth only
                    std::memcpy(dir_data + entry_offset, &new_local_depth, 4);
                }
            }
        }

        // Update old bucket's local depth
        std::memcpy(bucket_data, &new_local_depth, 4);

        new_bucket_page->MarkDirty();
        bpm_->UnpinPage(new_bucket_id, true);

        // Redistribute entries
        // Clear old bucket
        std::memset(bucket_data + 4, 0, 2);
        bucket_size = 0;

        for (auto& [k, rid] : entries) {
            uint32_t hash_dir = HashKey(k) & ((1 << new_local_depth) - 1);
            page_id_t target_bucket;
            uint32_t entry_offset = 4 + (HashKey(k) & ((1 << global_depth) - 1)) * 8 + 4;
            std::memcpy(&target_bucket, dir_data + entry_offset, 4);

            if (static_cast<page_id_t>(target_bucket) == bucket_page_id) {
                // Re-insert into old bucket
                uint32_t off = BUCKET_HEADER_SIZE + bucket_size * BUCKET_KV_SIZE;
                std::memcpy(bucket_data + off, &k, 8);
                std::memcpy(bucket_data + off + 8, &rid.page_id, 4);
                std::memcpy(bucket_data + off + 12, &rid.slot_id, 2);
                bucket_size++;
                std::memcpy(bucket_data + 4, &bucket_size, 2);
            } else {
                // Insert into new bucket
                char* nb_data = new_bucket_page->GetData();
                uint16_t nb_size;
                std::memcpy(&nb_size, nb_data + 4, 2);
                uint32_t off = BUCKET_HEADER_SIZE + nb_size * BUCKET_KV_SIZE;
                std::memcpy(nb_data + off, &k, 8);
                std::memcpy(nb_data + off + 8, &rid.page_id, 4);
                std::memcpy(nb_data + off + 12, &rid.slot_id, 2);
                nb_size++;
                std::memcpy(nb_data + 4, &nb_size, 2);
            }
        }
    } else {
        // local_depth == global_depth: need to double directory
        bpm_->UnpinPage(bucket_page_id, false);
        bpm_->UnpinPage(directory_page_id_, false);
        DoubleDirectory();
        // Retry split after doubling
        SplitBucket(bucket_page_id);
        return;
    }

    bucket_page->MarkDirty();
    dir_page->MarkDirty();
    bpm_->UnpinPage(bucket_page_id, true);
    bpm_->UnpinPage(directory_page_id_, true);
}

void ExtendibleHashIndex::DoubleDirectory() {
    Page* dir_page = bpm_->FetchPage(directory_page_id_);
    if (!dir_page) return;

    char* dir_data = dir_page->GetData();
    uint32_t global_depth;
    std::memcpy(&global_depth, dir_data, 4);

    // Double the directory entries
    uint32_t old_size = 1 << global_depth;
    uint32_t new_global_depth = global_depth + 1;
    std::memcpy(dir_data, &new_global_depth, 4);

    // Copy entries: each old entry i gets duplicated at positions i and i + old_size
    for (uint32_t i = old_size; i > 0; i--) {
        uint32_t old_idx = i - 1;
        uint32_t old_entry_offset = 4 + old_idx * 8;
        uint32_t new_entry_offset = 4 + (old_idx + old_size) * 8;

        // Copy [local_depth(4B), bucket_id(4B)]
        std::memcpy(dir_data + new_entry_offset, dir_data + old_entry_offset, 8);
    }

    dir_page->MarkDirty();
    bpm_->UnpinPage(directory_page_id_, true);
}

RID ExtendibleHashIndex::GetValue(int64_t key) {
    std::lock_guard<std::mutex> lock(mutex_);

    page_id_t bucket_id = GetBucketPageId(key);
    if (bucket_id == INVALID_PAGE_ID) return RID();

    Page* page = bpm_->FetchPage(bucket_id);
    if (!page) return RID();

    char* data = page->GetData();
    uint16_t bucket_size;
    std::memcpy(&bucket_size, data + 4, 2);

    RID result;
    for (uint16_t i = 0; i < bucket_size; i++) {
        uint32_t offset = BUCKET_HEADER_SIZE + i * BUCKET_KV_SIZE;
        int64_t k;
        std::memcpy(&k, data + offset, 8);
        if (k == key) {
            std::memcpy(&result.page_id, data + offset + 8, 4);
            std::memcpy(&result.slot_id, data + offset + 12, 2);
            break;
        }
    }

    bpm_->UnpinPage(bucket_id, false);
    return result;
}

bool ExtendibleHashIndex::Remove(int64_t key) {
    std::lock_guard<std::mutex> lock(mutex_);

    page_id_t bucket_id = GetBucketPageId(key);
    if (bucket_id == INVALID_PAGE_ID) return false;

    if (RemoveFromBucket(bucket_id, key)) {
        size_--;
        return true;
    }
    return false;
}

bool ExtendibleHashIndex::RemoveFromBucket(page_id_t bucket_page_id, int64_t key) {
    Page* page = bpm_->FetchPage(bucket_page_id);
    if (!page) return false;

    char* data = page->GetData();
    uint16_t bucket_size;
    std::memcpy(&bucket_size, data + 4, 2);

    // Find the key
    int16_t found_idx = -1;
    for (uint16_t i = 0; i < bucket_size; i++) {
        uint32_t offset = BUCKET_HEADER_SIZE + i * BUCKET_KV_SIZE;
        int64_t k;
        std::memcpy(&k, data + offset, 8);
        if (k == key) {
            found_idx = i;
            break;
        }
    }

    if (found_idx < 0) {
        bpm_->UnpinPage(bucket_page_id, false);
        return false;
    }

    // Shift remaining entries
    for (uint16_t i = found_idx; i < bucket_size - 1; i++) {
        uint32_t src = BUCKET_HEADER_SIZE + (i + 1) * BUCKET_KV_SIZE;
        uint32_t dst = BUCKET_HEADER_SIZE + i * BUCKET_KV_SIZE;
        std::memcpy(data + dst, data + src, BUCKET_KV_SIZE);
    }

    bucket_size--;
    std::memcpy(data + 4, &bucket_size, 2);

    page->MarkDirty();
    bpm_->UnpinPage(bucket_page_id, true);
    return true;
}

std::vector<std::pair<int64_t, RID>> ExtendibleHashIndex::RangeScan(
    int64_t start_key, int64_t end_key) {
    // Hash index doesn't support efficient range scans
    // Fall back to scanning all buckets (not efficient)
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<int64_t, RID>> results;

    if (directory_page_id_ == INVALID_PAGE_ID) return results;

    Page* dir_page = bpm_->FetchPage(directory_page_id_);
    if (!dir_page) return results;

    char* dir_data = dir_page->GetData();
    uint32_t global_depth;
    std::memcpy(&global_depth, dir_data, 4);

    uint32_t dir_size = 1 << global_depth;

    for (uint32_t i = 0; i < dir_size; i++) {
        uint32_t entry_offset = 4 + i * 8 + 4;  // bucket_id
        uint32_t bucket_id;
        std::memcpy(&bucket_id, dir_data + entry_offset, 4);

        Page* bucket_page = bpm_->FetchPage(static_cast<page_id_t>(bucket_id));
        if (!bucket_page) continue;

        char* bucket_data = bucket_page->GetData();
        uint16_t bucket_size;
        std::memcpy(&bucket_size, bucket_data + 4, 2);

        for (uint16_t j = 0; j < bucket_size; j++) {
            uint32_t offset = BUCKET_HEADER_SIZE + j * BUCKET_KV_SIZE;
            int64_t k;
            RID r;
            std::memcpy(&k, bucket_data + offset, 8);
            if (k >= start_key && k <= end_key) {
                std::memcpy(&r.page_id, bucket_data + offset + 8, 4);
                std::memcpy(&r.slot_id, bucket_data + offset + 12, 2);
                results.push_back({k, r});
            }
        }
        bpm_->UnpinPage(static_cast<page_id_t>(bucket_id), false);
    }

    bpm_->UnpinPage(directory_page_id_, false);
    return results;
}

}  // namespace goods_db
