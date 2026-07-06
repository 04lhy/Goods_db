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
#include "storage/page/b_plus_tree_internal_page.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/b_plus_tree_page.h"

namespace goods_db {

/**
 * BPlusTree - a B+Tree index implementation.
 *
 * Supports:
 *   - Insert(key, rid): inserts a KV pair, handling page splits
 *   - Remove(key): removes a KV pair, handling page merges/borrows
 *   - GetValue(key): point lookup, returns the RID
 *   - RangeScan(start, end): range scan using leaf sibling pointers
 *
 * The tree is stored across disk pages managed by the BufferPoolManager.
 * The root page_id is stored externally (in the IndexInfo / Catalog).
 */
class BPlusTree : public Index {
public:
    /**
     * @param bpm The buffer pool manager
     * @param root_page_id The page_id of the root (INVALID_PAGE_ID for empty tree)
     */
    BPlusTree(BufferPoolManager* bpm, page_id_t root_page_id = INVALID_PAGE_ID);

    ~BPlusTree() override = default;

    // Index interface
    bool Insert(int64_t key, const RID& rid) override;
    bool Remove(int64_t key) override;
    RID GetValue(int64_t key) override;
    std::vector<std::pair<int64_t, RID>> RangeScan(int64_t start_key,
                                                     int64_t end_key) override;
    size_t GetSize() const override { return size_; }
    const char* GetName() const override { return "BPlusTree"; }

    /** Get the current root page id */
    page_id_t GetRootPageId() const { return root_page_id_; }

private:
    // =========================================================================
    // Internal helpers
    // =========================================================================

    /** Find the leaf page that should contain the given key */
    page_id_t FindLeaf(int64_t key);

    /** Insert into a leaf page. Returns true if split occurred. */
    bool InsertIntoLeaf(page_id_t leaf_page_id, int64_t key, const RID& rid);

    /** Insert into parent after a split. key is the first key of the new child. */
    void InsertIntoParent(page_id_t left_page_id, int64_t key,
                          page_id_t right_page_id);

    /** Handle leaf page overflow by splitting */
    void SplitLeaf(page_id_t leaf_page_id);

    /** Handle internal page overflow by splitting */
    void SplitInternal(page_id_t internal_page_id);

    /** Create a new root page */
    void CreateNewRoot(page_id_t left_child, int64_t key, page_id_t right_child);

    /** Remove from a leaf. Returns true if page needs coalesce/redistribute. */
    bool RemoveFromLeaf(page_id_t leaf_page_id, int64_t key);

    /** Handle underflow by borrowing from sibling or merging */
    void HandleUnderflow(page_id_t page_id);

    /** Update the root if it became empty */
    void AdjustRoot();

    /** Convert raw page data to typed page */
    BPlusTreeLeafPage ToLeafPage(char* data);
    BPlusTreeInternalPage ToInternalPage(char* data);

    BufferPoolManager* bpm_;
    page_id_t root_page_id_;
    size_t size_{0};
    mutable std::mutex mutex_;
};

}  // namespace goods_db
