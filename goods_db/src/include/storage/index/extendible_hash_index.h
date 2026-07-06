#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "buffer/buffer_pool_manager.h"
#include "common/config.h"
#include "common/rid.h"
#include "storage/index/index.h"

namespace goods_db {

/**
 * Extendible Hash Index.
 *
 * Uses a directory of bucket pointers. Each bucket stores key-rid pairs.
 * When a bucket overflows:
 *   - If local_depth < global_depth: split the bucket (increment local_depth),
 *     redistribute entries using new hash bits.
 *   - If local_depth == global_depth: double the directory (increment global_depth).
 *
 * Directory page layout:
 *   [global_depth (4B)] [local_depth₀ (4B)] [bucket_page_id₀ (4B)] ...
 *   Directory size = 1 << global_depth entries
 *
 * Bucket page layout:
 *   [local_depth (4B)] [size (2B)] [KV pairs...]
 *   Each KV pair: key (8B) + rid (6B) = 14B
 *   Max pairs per bucket = (PAGE_SIZE - 6) / 14 ≈ 1169
 */
class ExtendibleHashIndex : public Index {
public:
    static constexpr uint32_t BUCKET_HEADER_SIZE = 6;  // local_depth + size
    static constexpr uint32_t BUCKET_KV_SIZE = 14;     // key + rid
    static constexpr uint32_t MAX_BUCKET_SIZE =
        (PAGE_SIZE - BUCKET_HEADER_SIZE) / BUCKET_KV_SIZE;

    /**
     * @param bpm The buffer pool manager
     * @param directory_page_id The page_id of the directory page
     *   (will be created on first insert if INVALID_PAGE_ID)
     */
    ExtendibleHashIndex(BufferPoolManager* bpm,
                        page_id_t directory_page_id = INVALID_PAGE_ID);

    ~ExtendibleHashIndex() override = default;

    // Index interface
    bool Insert(int64_t key, const RID& rid) override;
    bool Remove(int64_t key) override;
    RID GetValue(int64_t key) override;
    std::vector<std::pair<int64_t, RID>> RangeScan(int64_t start_key,
                                                     int64_t end_key) override;
    size_t GetSize() const override { return size_; }
    const char* GetName() const override { return "ExtendibleHash"; }

    /** Get the directory page id */
    page_id_t GetDirectoryPageId() const { return directory_page_id_; }

private:
    /** Hash a key to a directory index. Uses murmur3 hash. */
    uint32_t HashKey(int64_t key) const;

    /** Get the bucket page_id for a key */
    page_id_t GetBucketPageId(int64_t key);

    /** Split a bucket (increment local_depth, redistribute entries) */
    void SplitBucket(page_id_t bucket_page_id);

    /** Double the directory size (increment global_depth) */
    void DoubleDirectory();

    /** Insert a KV into a bucket page. Returns false if bucket is full. */
    bool InsertIntoBucket(page_id_t bucket_page_id, int64_t key, const RID& rid);

    /** Remove a key from a bucket page. Returns true if found and removed. */
    bool RemoveFromBucket(page_id_t bucket_page_id, int64_t key);

    BufferPoolManager* bpm_;
    page_id_t directory_page_id_;
    size_t size_{0};
    mutable std::mutex mutex_;
};

}  // namespace goods_db
