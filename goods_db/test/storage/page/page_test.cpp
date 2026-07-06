#include <gtest/gtest.h>
#include <cstring>
#include <memory>
#include "test/common/test_common.h"
#include "storage/page/table_page.h"
#include "storage/page/page.h"
#include "common/config.h"

using namespace goods_db;

TEST(PageTest, Initialize) {
    auto page_data = std::make_unique<char[]>(PAGE_SIZE);
    std::memset(page_data.get(), 0, PAGE_SIZE);

    Page page;
    EXPECT_EQ(page.GetPageId(), INVALID_PAGE_ID);
    EXPECT_EQ(page.GetPinCount(), 0);
    EXPECT_FALSE(page.IsDirty());
}

TEST(PageTest, PinUnpin) {
    Page page;
    page.Pin();
    EXPECT_EQ(page.GetPinCount(), 1);
    page.Pin();
    EXPECT_EQ(page.GetPinCount(), 2);
    page.Unpin();
    EXPECT_EQ(page.GetPinCount(), 1);
}

TEST(TablePageTest, Init) {
    auto page_data = std::make_unique<char[]>(PAGE_SIZE);
    std::memset(page_data.get(), 0, PAGE_SIZE);

    TablePage tp;
    tp.LoadFromData(page_data.get());
    tp.Init(100);

    EXPECT_EQ(tp.GetPageId(), 100);
    EXPECT_EQ(tp.GetSlotCount(), 0);
    EXPECT_EQ(tp.GetNextPageId(), INVALID_PAGE_ID);
    EXPECT_EQ(tp.GetPrevPageId(), INVALID_PAGE_ID);
    EXPECT_GT(tp.GetFreeSpace(), 0u);
}

TEST(TablePageTest, InsertAndGetTuple) {
    auto page_data = std::make_unique<char[]>(PAGE_SIZE);
    std::memset(page_data.get(), 0, PAGE_SIZE);

    TablePage tp;
    tp.LoadFromData(page_data.get());
    tp.Init(1);

    const char* test_data = "Hello, TablePage!";
    int16_t slot_id;
    ASSERT_TRUE(tp.InsertTuple(test_data, std::strlen(test_data) + 1, &slot_id));
    EXPECT_EQ(slot_id, 0);

    char buf[100];
    uint32_t size;
    ASSERT_TRUE(tp.GetTuple(slot_id, buf, &size));
    EXPECT_EQ(size, std::strlen(test_data) + 1);
    EXPECT_STREQ(buf, test_data);
}

TEST(TablePageTest, UpdateTuple) {
    auto page_data = std::make_unique<char[]>(PAGE_SIZE);
    std::memset(page_data.get(), 0, PAGE_SIZE);

    TablePage tp;
    tp.LoadFromData(page_data.get());
    tp.Init(1);

    const char* original = "Original";
    int16_t slot_id;
    ASSERT_TRUE(tp.InsertTuple(original, std::strlen(original) + 1, &slot_id));

    // Update with smaller data (should succeed in-place)
    const char* smaller = "New";
    EXPECT_TRUE(tp.UpdateTuple(slot_id, smaller, std::strlen(smaller) + 1));

    char buf[100];
    uint32_t size;
    ASSERT_TRUE(tp.GetTuple(slot_id, buf, &size));
    EXPECT_STREQ(buf, "New");
}

TEST(TablePageTest, MarkAndCheckDeleted) {
    auto page_data = std::make_unique<char[]>(PAGE_SIZE);
    std::memset(page_data.get(), 0, PAGE_SIZE);

    TablePage tp;
    tp.LoadFromData(page_data.get());
    tp.Init(1);

    const char* data = "temp";
    int16_t slot_id;
    ASSERT_TRUE(tp.InsertTuple(data, std::strlen(data) + 1, &slot_id));
    EXPECT_FALSE(tp.IsDeleted(slot_id));

    ASSERT_TRUE(tp.MarkDelete(slot_id));
    EXPECT_TRUE(tp.IsDeleted(slot_id));
}

TEST(TablePageTest, MultipleInserts) {
    auto page_data = std::make_unique<char[]>(PAGE_SIZE);
    std::memset(page_data.get(), 0, PAGE_SIZE);

    TablePage tp;
    tp.LoadFromData(page_data.get());
    tp.Init(1);

    // Insert many small tuples
    char data[50];
    for (int i = 0; i < 100; i++) {
        std::snprintf(data, sizeof(data), "Tuple_%d", i);
        int16_t slot_id;
        ASSERT_TRUE(tp.InsertTuple(data, std::strlen(data) + 1, &slot_id));
        EXPECT_EQ(slot_id, i);
    }

    // Verify all can be read
    for (int16_t i = 0; i < 100; i++) {
        EXPECT_FALSE(tp.IsDeleted(i));
        char buf[50];
        uint32_t size;
        char expected[50];
        std::snprintf(expected, sizeof(expected), "Tuple_%d", i);
        ASSERT_TRUE(tp.GetTuple(i, buf, &size));
        EXPECT_STREQ(buf, expected);
    }
}
