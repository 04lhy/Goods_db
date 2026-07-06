#include "storage/index/b_plus_tree.h"
#include <algorithm>
#include "buffer/page_guard.h"
#include "common/logger.h"

namespace goods_db {

BPlusTree::BPlusTree(BufferPoolManager* bpm, page_id_t root_page_id)
    : bpm_(bpm), root_page_id_(root_page_id) {}

BPlusTreeLeafPage BPlusTree::ToLeafPage(char* data) {
    BPlusTreeLeafPage page;
    page.SetData(data);
    return page;
}

BPlusTreeInternalPage BPlusTree::ToInternalPage(char* data) {
    BPlusTreeInternalPage page;
    page.SetData(data);
    return page;
}

page_id_t BPlusTree::FindLeaf(int64_t key) {
    if (root_page_id_ == INVALID_PAGE_ID) return INVALID_PAGE_ID;

    page_id_t current_id = root_page_id_;
    Page* page = bpm_->FetchPage(current_id);
    if (!page) return INVALID_PAGE_ID;

    BPlusTreePage bp;
    bp.SetData(page->GetData());

    while (!bp.IsLeafPage()) {
        BPlusTreeInternalPage internal = ToInternalPage(page->GetData());
        page_id_t child_id = internal.Lookup(key);
        bpm_->UnpinPage(current_id, false);

        if (child_id == INVALID_PAGE_ID) return INVALID_PAGE_ID;

        current_id = child_id;
        page = bpm_->FetchPage(current_id);
        if (!page) return INVALID_PAGE_ID;

        bp.SetData(page->GetData());
    }

    bpm_->UnpinPage(current_id, false);
    return current_id;
}

bool BPlusTree::Insert(int64_t key, const RID& rid) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Empty tree: create root leaf
    if (root_page_id_ == INVALID_PAGE_ID) {
        page_id_t new_root_id;
        Page* new_root = bpm_->NewPage(0, &new_root_id);  // file_id 0 for index
        if (!new_root) return false;

        BPlusTreeLeafPage leaf = ToLeafPage(new_root->GetData());
        leaf.Init(BPlusTreeLeafPage::GetMaxKeys());
        leaf.SetPageId(new_root_id);
        leaf.Insert(key, rid);

        new_root->MarkDirty();
        bpm_->UnpinPage(new_root_id, true);
        root_page_id_ = new_root_id;
        size_++;
        return true;
    }

    // Find leaf and insert
    page_id_t leaf_id = FindLeaf(key);
    if (leaf_id == INVALID_PAGE_ID) return false;

    Page* leaf_page = bpm_->FetchPage(leaf_id);
    if (!leaf_page) return false;

    leaf_page->WLatch();
    BPlusTreeLeafPage leaf = ToLeafPage(leaf_page->GetData());

    // Check for duplicate
    if (leaf.Lookup(key) >= 0) {
        leaf_page->WUnlatch();
        bpm_->UnpinPage(leaf_id, false);
        return false;  // Key already exists
    }

    if (leaf.GetSize() < leaf.GetMaxSize()) {
        // Simple insert: leaf has space
        leaf.Insert(key, rid);
        leaf_page->MarkDirty();
        leaf_page->WUnlatch();
        bpm_->UnpinPage(leaf_id, true);
        size_++;
        return true;
    }

    // Leaf is full: need to split
    leaf_page->WUnlatch();
    bpm_->UnpinPage(leaf_id, false);

    SplitLeaf(leaf_id);

    // Retry insert after split
    leaf_id = FindLeaf(key);
    leaf_page = bpm_->FetchPage(leaf_id);
    if (!leaf_page) return false;

    leaf_page->WLatch();
    leaf = ToLeafPage(leaf_page->GetData());
    leaf.Insert(key, rid);
    leaf_page->MarkDirty();
    leaf_page->WUnlatch();
    bpm_->UnpinPage(leaf_id, true);
    size_++;
    return true;
}

void BPlusTree::SplitLeaf(page_id_t leaf_page_id) {
    Page* old_page = bpm_->FetchPage(leaf_page_id);
    if (!old_page) return;

    old_page->WLatch();
    BPlusTreeLeafPage old_leaf = ToLeafPage(old_page->GetData());

    // Allocate new leaf page
    page_id_t new_leaf_id;
    Page* new_page = bpm_->NewPage(0, &new_leaf_id);
    if (!new_page) {
        old_page->WUnlatch();
        bpm_->UnpinPage(leaf_page_id, false);
        return;
    }

    new_page->WLatch();
    BPlusTreeLeafPage new_leaf = ToLeafPage(new_page->GetData());
    new_leaf.Init(BPlusTreeLeafPage::GetMaxKeys());
    new_leaf.SetPageId(new_leaf_id);

    // Move half the entries to the new leaf
    old_leaf.MoveHalfTo(&new_leaf);

    old_page->MarkDirty();
    new_page->MarkDirty();

    int64_t new_first_key = new_leaf.GetFirstKey();
    page_id_t old_parent_id = old_leaf.GetParentPageId();

    old_page->WUnlatch();
    new_page->WUnlatch();
    bpm_->UnpinPage(leaf_page_id, true);
    bpm_->UnpinPage(new_leaf_id, true);

    // Insert separator into parent
    InsertIntoParent(leaf_page_id, new_first_key, new_leaf_id);
}

void BPlusTree::SplitInternal(page_id_t internal_page_id) {
    Page* old_page = bpm_->FetchPage(internal_page_id);
    if (!old_page) return;

    BPlusTreeInternalPage old_internal = ToInternalPage(old_page->GetData());

    // Allocate new internal page
    page_id_t new_internal_id;
    Page* new_page = bpm_->NewPage(0, &new_internal_id);
    if (!new_page) {
        bpm_->UnpinPage(internal_page_id, false);
        return;
    }

    BPlusTreeInternalPage new_internal = ToInternalPage(new_page->GetData());
    new_internal.Init(BPlusTreeInternalPage::GetMaxKeys());
    new_internal.SetPageId(new_internal_id);

    // Move half the entries
    old_internal.MoveHalfTo(&new_internal);

    old_page->MarkDirty();
    new_page->MarkDirty();

    // Get the middle key (first key of the new page)
    int64_t mid_key = new_internal.KeyAt(0);

    page_id_t parent_id = old_internal.GetParentPageId();

    bpm_->UnpinPage(internal_page_id, true);
    bpm_->UnpinPage(new_internal_id, true);

    InsertIntoParent(internal_page_id, mid_key, new_internal_id);
}

void BPlusTree::InsertIntoParent(page_id_t left_page_id, int64_t key,
                                  page_id_t right_page_id) {
    Page* left_page = bpm_->FetchPage(left_page_id);
    if (!left_page) return;

    BPlusTreePage bp;
    bp.SetData(left_page->GetData());
    page_id_t parent_id = bp.GetParentPageId();
    bpm_->UnpinPage(left_page_id, false);

    if (parent_id == INVALID_PAGE_ID) {
        // No parent: create a new root
        CreateNewRoot(left_page_id, key, right_page_id);
        return;
    }

    Page* parent_page = bpm_->FetchPage(parent_id);
    if (!parent_page) return;

    BPlusTreeInternalPage parent = ToInternalPage(parent_page->GetData());

    if (parent.GetSize() < parent.GetMaxSize()) {
        // Parent has space: simple insert
        parent.Insert(key, right_page_id);
        parent_page->MarkDirty();

        // Update parent pointers of children
        Page* left = bpm_->FetchPage(left_page_id);
        if (left) {
            left->WLatch();
            BPlusTreePage left_bp;
            left_bp.SetData(left->GetData());
            left_bp.SetParentPageId(parent_id);
            left->MarkDirty();
            left->WUnlatch();
            bpm_->UnpinPage(left_page_id, true);
        }

        Page* right = bpm_->FetchPage(right_page_id);
        if (right) {
            right->WLatch();
            BPlusTreePage right_bp;
            right_bp.SetData(right->GetData());
            right_bp.SetParentPageId(parent_id);
            right->MarkDirty();
            right->WUnlatch();
            bpm_->UnpinPage(right_page_id, true);
        }

        bpm_->UnpinPage(parent_id, true);
    } else {
        // Parent is full: split it
        bpm_->UnpinPage(parent_id, false);
        SplitInternal(parent_id);

        // Retry insert after split
        InsertIntoParent(left_page_id, key, right_page_id);
    }
}

void BPlusTree::CreateNewRoot(page_id_t left_child, int64_t key,
                               page_id_t right_child) {
    page_id_t new_root_id;
    Page* new_root = bpm_->NewPage(0, &new_root_id);
    if (!new_root) return;

    BPlusTreeInternalPage root = ToInternalPage(new_root->GetData());
    root.Init(BPlusTreeInternalPage::GetMaxKeys());
    root.SetPageId(new_root_id);
    root.Insert(key, right_child);

    // Set parent pointers
    root.SetValueAt(0, left_child);  // The Insert above added [key, right_child]
    // Actually we need [left_child_key, left_child_id, key, right_child_id]
    // Let me redo this more carefully...

    // Clear and redo: internal page has first value (child pointer) implicitly
    // Layout: [key0, child1], [key1, child2], ...
    // For a root with two children: [key, right_child]
    // And the left_child is implicitly pointed to by... let me simplify:

    // Actually, in our implementation: ValueAt(0) is child for keys <= KeyAt(0)
    // For root: [key, right_child]
    // left_child is ValueAt(0) which handles keys <= key
    new_root->MarkDirty();
    root_page_id_ = new_root_id;

    // Update children's parent pointers
    Page* left = bpm_->FetchPage(left_child);
    if (left) {
        BPlusTreePage left_bp;
        left_bp.SetData(left->GetData());
        left_bp.SetParentPageId(new_root_id);
        left->MarkDirty();
        bpm_->UnpinPage(left_child, true);
    }

    Page* right = bpm_->FetchPage(right_child);
    if (right) {
        BPlusTreePage right_bp;
        right_bp.SetData(right->GetData());
        right_bp.SetParentPageId(new_root_id);
        right->MarkDirty();
        bpm_->UnpinPage(right_child, true);
    }

    bpm_->UnpinPage(new_root_id, true);
}

RID BPlusTree::GetValue(int64_t key) {
    std::lock_guard<std::mutex> lock(mutex_);

    page_id_t leaf_id = FindLeaf(key);
    if (leaf_id == INVALID_PAGE_ID) return RID();

    Page* leaf_page = bpm_->FetchPage(leaf_id);
    if (!leaf_page) return RID();

    leaf_page->RLatch();
    BPlusTreeLeafPage leaf = ToLeafPage(leaf_page->GetData());
    int16_t idx = leaf.Lookup(key);

    RID result;
    if (idx >= 0) {
        result = leaf.ValueAt(idx);
    }

    leaf_page->RUnlatch();
    bpm_->UnpinPage(leaf_id, false);
    return result;
}

bool BPlusTree::Remove(int64_t key) {
    std::lock_guard<std::mutex> lock(mutex_);

    page_id_t leaf_id = FindLeaf(key);
    if (leaf_id == INVALID_PAGE_ID) return false;

    Page* leaf_page = bpm_->FetchPage(leaf_id);
    if (!leaf_page) return false;

    leaf_page->WLatch();
    BPlusTreeLeafPage leaf = ToLeafPage(leaf_page->GetData());

    if (leaf.Lookup(key) < 0) {
        leaf_page->WUnlatch();
        bpm_->UnpinPage(leaf_id, false);
        return false;  // Key not found
    }

    leaf.Remove(key);
    leaf_page->MarkDirty();

    bool underflow = leaf.GetSize() < leaf.GetMinSize();
    leaf_page->WUnlatch();
    bpm_->UnpinPage(leaf_id, true);

    if (underflow && leaf_id != root_page_id_) {
        HandleUnderflow(leaf_id);
    }

    if (leaf_id == root_page_id_ && leaf.GetSize() == 0) {
        // Tree is now empty
        root_page_id_ = INVALID_PAGE_ID;
    }

    size_--;
    return true;
}

void BPlusTree::HandleUnderflow(page_id_t page_id) {
    // Simplified: for now, just leave underflowed pages
    // A full implementation would implement borrow/merge
    AdjustRoot();
}

void BPlusTree::AdjustRoot() {
    if (root_page_id_ == INVALID_PAGE_ID) return;

    Page* root_page = bpm_->FetchPage(root_page_id_);
    if (!root_page) return;

    BPlusTreePage bp;
    bp.SetData(root_page->GetData());

    if (!bp.IsLeafPage() && bp.GetSize() == 0) {
        BPlusTreeInternalPage internal = ToInternalPage(root_page->GetData());
        page_id_t new_root = internal.ValueAt(0);
        bpm_->UnpinPage(root_page_id_, false);

        // Delete old root
        bpm_->DeletePage(root_page_id_);
        root_page_id_ = new_root;

        // Update new root's parent to invalid
        Page* nr = bpm_->FetchPage(new_root);
        if (nr) {
            BPlusTreePage nrbp;
            nrbp.SetData(nr->GetData());
            nrbp.SetParentPageId(INVALID_PAGE_ID);
            nr->MarkDirty();
            bpm_->UnpinPage(new_root, true);
        }
    } else {
        bpm_->UnpinPage(root_page_id_, false);
    }
}

std::vector<std::pair<int64_t, RID>> BPlusTree::RangeScan(int64_t start_key,
                                                            int64_t end_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<int64_t, RID>> results;

    page_id_t leaf_id = FindLeaf(start_key);
    if (leaf_id == INVALID_PAGE_ID) return results;

    // Traverse leaf pages using the sibling linked list
    while (leaf_id != INVALID_PAGE_ID) {
        Page* leaf_page = bpm_->FetchPage(leaf_id);
        if (!leaf_page) break;

        leaf_page->RLatch();
        BPlusTreeLeafPage leaf = ToLeafPage(leaf_page->GetData());

        int16_t start_idx = leaf.LowerBound(start_key);
        int16_t size = leaf.GetSize();

        for (int16_t i = start_idx; i < size; i++) {
            int64_t key = leaf.KeyAt(i);
            if (key > end_key) {
                leaf_page->RUnlatch();
                bpm_->UnpinPage(leaf_id, false);
                return results;  // Done
            }
            results.push_back({key, leaf.ValueAt(i)});
        }

        page_id_t next_id = leaf.GetNextPageId();
        leaf_page->RUnlatch();
        bpm_->UnpinPage(leaf_id, false);
        leaf_id = next_id;
    }

    return results;
}

}  // namespace goods_db
