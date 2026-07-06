#include <gtest/gtest.h>
#include "test/common/test_common.h"
#include "storage/index/b_plus_tree.h"
#include "storage/index/extendible_hash_index.h"
#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "storage/disk/disk_manager.h"

using namespace goods_db;

class IndexTest : public ::testing::Test {
protected:
    void SetUp() override {
        dm_ = std::make_unique<DiskManager>();
        dm_->SetDataDir("./test_idx_data");
        system("mkdir -p ./test_idx_data");

        // Create a file for index page allocation (file_id=0)
        file_id_ = dm_->CreateFile("index_data", "./test_idx_data/index_data.db");

        auto replacer = std::make_unique<ClockReplacer>(50);
        bpm_ = std::make_unique<BufferPoolManager>(50, dm_.get(),
                                                    std::move(replacer));
    }

    void TearDown() override {
        bpm_.reset();
        dm_.reset();
        system("rm -rf ./test_idx_data");
    }

    std::unique_ptr<DiskManager> dm_;
    std::unique_ptr<BufferPoolManager> bpm_;
    uint16_t file_id_{0};
};

// =============================================================================
// B+Tree Tests
// =============================================================================

TEST_F(IndexTest, BPlusTreeInsertAndLookup) {
    BPlusTree tree(bpm_.get());

    // Insert keys
    for (int64_t i = 1; i <= 100; i++) {
        RID rid(static_cast<int32_t>(i), 0);
        ASSERT_TRUE(tree.Insert(i, rid)) << "Insert key " << i;
    }

    // Lookup each key
    for (int64_t i = 1; i <= 100; i++) {
        RID rid = tree.GetValue(i);
        EXPECT_EQ(rid.page_id, static_cast<int32_t>(i))
            << "Lookup key " << i;
    }

    // Lookup non-existent key
    RID rid = tree.GetValue(999);
    EXPECT_EQ(rid.page_id, 0);
}

TEST_F(IndexTest, BPlusTreeDelete) {
    BPlusTree tree(bpm_.get());

    // Insert
    for (int64_t i = 1; i <= 50; i++) {
        ASSERT_TRUE(tree.Insert(i, RID(i, 0)));
    }

    // Delete some keys
    for (int64_t i = 10; i <= 20; i++) {
        ASSERT_TRUE(tree.Remove(i));
    }

    // Verify deleted keys are gone
    for (int64_t i = 10; i <= 20; i++) {
        RID rid = tree.GetValue(i);
        EXPECT_EQ(rid.page_id, 0) << "Key " << i << " should be deleted";
    }

    // Verify remaining keys still exist
    for (int64_t i = 1; i <= 9; i++) {
        RID rid = tree.GetValue(i);
        EXPECT_NE(rid.page_id, 0) << "Key " << i << " should exist";
    }
}

TEST_F(IndexTest, BPlusTreeRangeScan) {
    BPlusTree tree(bpm_.get());

    for (int64_t i = 1; i <= 50; i++) {
        ASSERT_TRUE(tree.Insert(i, RID(i, 0)));
    }

    auto results = tree.RangeScan(20, 30);
    EXPECT_EQ(results.size(), 11u);

    // Verify sorted
    for (size_t i = 1; i < results.size(); i++) {
        EXPECT_LT(results[i - 1].first, results[i].first);
    }

    // Verify all keys in range
    for (int64_t i = 20; i <= 30; i++) {
        bool found = false;
        for (const auto& [key, rid] : results) {
            if (key == i) { found = true; break; }
        }
        EXPECT_TRUE(found) << "Key " << i << " missing from range scan";
    }
}

TEST_F(IndexTest, BPlusTreeLargeInsert) {
    BPlusTree tree(bpm_.get());

    const int N = 200;
    for (int64_t i = 1; i <= N; i++) {
        ASSERT_TRUE(tree.Insert(i, RID(i, 0))) << "Insert " << i;
    }

    // Verify all
    for (int64_t i = 1; i <= N; i++) {
        RID rid = tree.GetValue(i);
        EXPECT_NE(rid.page_id, 0) << "Missing key " << i;
    }
}

// =============================================================================
// Extendible Hash Index Tests
// =============================================================================

TEST_F(IndexTest, HashIndexInsertAndLookup) {
    ExtendibleHashIndex hash(bpm_.get());

    for (int64_t i = 1; i <= 100; i++) {
        ASSERT_TRUE(hash.Insert(i, RID(i, 0))) << "Insert " << i;
    }

    for (int64_t i = 1; i <= 100; i++) {
        RID rid = hash.GetValue(i);
        EXPECT_EQ(rid.page_id, static_cast<int32_t>(i));
    }

    RID rid = hash.GetValue(9999);
    EXPECT_EQ(rid.page_id, 0);
}

TEST_F(IndexTest, HashIndexDelete) {
    ExtendibleHashIndex hash(bpm_.get());

    for (int64_t i = 1; i <= 20; i++) {
        ASSERT_TRUE(hash.Insert(i, RID(i, 0)));
    }

    for (int64_t i = 5; i <= 10; i++) {
        ASSERT_TRUE(hash.Remove(i));
    }

    for (int64_t i = 5; i <= 10; i++) {
        RID rid = hash.GetValue(i);
        EXPECT_EQ(rid.page_id, 0) << "Should be deleted";
    }

    for (int64_t i = 1; i <= 4; i++) {
        RID rid = hash.GetValue(i);
        EXPECT_NE(rid.page_id, 0) << "Should exist";
    }
}
