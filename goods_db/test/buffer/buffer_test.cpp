#include <gtest/gtest.h>
#include <thread>
#include "test/common/test_common.h"
#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "buffer/lru_k_replacer.h"
#include "buffer/page_guard.h"
#include "storage/disk/disk_manager.h"
#include "common/config.h"

using namespace goods_db;

// =============================================================================
// Test Fixture
// =============================================================================

class BufferPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        dm_ = std::make_unique<DiskManager>();
        dm_->SetDataDir("./test_buf_data");
        system("mkdir -p ./test_buf_data");

        // Create a test file
        file_id_ = dm_->CreateFile("test_table", "./test_buf_data/test_table.db");
        ASSERT_NE(file_id_, static_cast<uint16_t>(-1));
    }

    void TearDown() override {
        dm_.reset();
        system("rm -rf ./test_buf_data");
    }

    std::unique_ptr<DiskManager> dm_;
    uint16_t file_id_{0};
};

// =============================================================================
// Clock Replacer Tests
// =============================================================================

TEST_F(BufferPoolTest, ClockReplacerBasic) {
    ClockReplacer replacer(10);

    // Initially no victims
    frame_id_t victim;
    EXPECT_FALSE(replacer.Victim(&victim));

    // Unpin some frames
    replacer.Unpin(0);
    replacer.Unpin(1);
    replacer.Unpin(2);
    EXPECT_EQ(replacer.Size(), 3u);

    // Clock algorithm: ref_bit is set on Unpin, so first Victim sweeps
    // through all frames clearing ref_bits. Second Victim finds one.
    bool found = false;
    for (int i = 0; i < 10; i++) {
        if (replacer.Victim(&victim)) {
            found = true;
            EXPECT_LT(victim, 10);
            break;
        }
    }
    EXPECT_TRUE(found) << "Should find victim after at most 2 full sweeps";

    // Pin removes frame from evictable set
    replacer.Pin(0);
    // After evicting one frame and pinning frame 0: 1 remaining evictable
    EXPECT_LE(replacer.Size(), 2u);
}

// =============================================================================
// LRU-K Replacer Tests
// =============================================================================

TEST_F(BufferPoolTest, LRUKReplacerBasic) {
    LRUKReplacer replacer(10, 2);

    frame_id_t victim;
    EXPECT_FALSE(replacer.Victim(&victim));

    replacer.Unpin(0);
    replacer.Unpin(1);
    EXPECT_EQ(replacer.Size(), 2u);

    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(replacer.Size(), 1u);
}

// =============================================================================
// BufferPoolManager Tests
// =============================================================================

TEST_F(BufferPoolTest, NewPageAndFetch) {
    auto replacer = std::make_unique<ClockReplacer>(10);
    BufferPoolManager bpm(10, dm_.get(), std::move(replacer));

    // Allocate a new page
    page_id_t page_id;
    Page* page = bpm.NewPage(file_id_, &page_id);
    ASSERT_NE(page, nullptr);
    EXPECT_NE(page_id, INVALID_PAGE_ID);
    EXPECT_EQ(page->GetPinCount(), 1);

    // Write data to page
    const char* test_data = "Hello, goods_db!";
    std::memcpy(page->GetData(), test_data, std::strlen(test_data) + 1);
    page->MarkDirty();

    // Unpin
    EXPECT_TRUE(bpm.UnpinPage(page_id, true));

    // Fetch again and verify data
    Page* fetched = bpm.FetchPage(page_id);
    ASSERT_NE(fetched, nullptr);
    EXPECT_STREQ(fetched->GetData(), test_data);
    EXPECT_EQ(fetched->GetPinCount(), 1);

    EXPECT_TRUE(bpm.UnpinPage(page_id, false));
}

TEST_F(BufferPoolTest, ConcurrentFetchUnpin) {
    auto replacer = std::make_unique<ClockReplacer>(10);
    BufferPoolManager bpm(10, dm_.get(), std::move(replacer));

    page_id_t page_id;
    Page* page = bpm.NewPage(file_id_, &page_id);
    ASSERT_NE(page, nullptr);
    ASSERT_TRUE(bpm.UnpinPage(page_id, false));

    std::vector<std::thread> threads;
    std::atomic<int> success{0};

    for (int t = 0; t < 4; t++) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 100; i++) {
                Page* p = bpm.FetchPage(page_id);
                if (p) {
                    success++;
                    bpm.UnpinPage(page_id, false);
                }
            }
        });
    }

    for (auto& t : threads) t.join();
    EXPECT_EQ(success, 400);
}

TEST_F(BufferPoolTest, FlushAllPages) {
    auto replacer = std::make_unique<ClockReplacer>(5);
    BufferPoolManager bpm(5, dm_.get(), std::move(replacer));

    // Allocate several pages
    std::vector<page_id_t> page_ids;
    for (int i = 0; i < 5; i++) {
        page_id_t pid;
        Page* p = bpm.NewPage(file_id_, &pid);
        ASSERT_NE(p, nullptr);
        std::memset(p->GetData(), i, PAGE_SIZE);
        p->MarkDirty();
        bpm.UnpinPage(pid, true);
        page_ids.push_back(pid);
    }

    // Flush all
    bpm.FlushAllPages();

    // All pages should be clean now
    EXPECT_EQ(bpm.GetDirtyPageCount(), 0u);
}

// =============================================================================
// PageGuard Tests
// =============================================================================

TEST_F(BufferPoolTest, WritePageGuard) {
    auto replacer = std::make_unique<ClockReplacer>(10);
    BufferPoolManager bpm(10, dm_.get(), std::move(replacer));

    page_id_t page_id;
    {
        Page* page = bpm.NewPage(file_id_, &page_id);
        ASSERT_NE(page, nullptr);
        bpm.UnpinPage(page_id, false);
    }

    {
        Page* page = bpm.FetchPage(page_id);
        ASSERT_NE(page, nullptr);

        WritePageGuard guard(&bpm, page);
        std::strcpy(guard.GetData(), "PageGuard test");
        guard.MarkDirty();
    }

    // Verify data persisted
    Page* page = bpm.FetchPage(page_id);
    ASSERT_NE(page, nullptr);
    EXPECT_STREQ(page->GetData(), "PageGuard test");
    bpm.UnpinPage(page_id, false);
}
