#include <gtest/gtest.h>
#include "test/common/test_common.h"
#include "storage/table/table_heap.h"
#include "storage/table/table_iterator.h"
#include "storage/table/tuple.h"
#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "storage/disk/disk_manager.h"
#include "type/schema.h"
#include "type/value.h"

using namespace goods_db;

class TableHeapTest : public ::testing::Test {
protected:
    void SetUp() override {
        dm_ = std::make_unique<DiskManager>();
        dm_->SetDataDir("./test_th_data");
        system("mkdir -p ./test_th_data");
        file_id_ = dm_->CreateFile("test", "./test_th_data/test.db");
        ASSERT_NE(file_id_, static_cast<uint16_t>(-1));

        auto replacer = std::make_unique<ClockReplacer>(20);
        bpm_ = std::make_unique<BufferPoolManager>(20, dm_.get(),
                                                    std::move(replacer));

        // Allocate first page
        first_page_id_ = dm_->AllocatePage(file_id_);
        ASSERT_NE(first_page_id_, INVALID_PAGE_ID);

        // Initialize first page
        Page* p = bpm_->FetchPage(first_page_id_);
        ASSERT_NE(p, nullptr);
        TablePage tp;
        tp.LoadFromData(p->GetData());
        tp.Init(first_page_id_);
        p->MarkDirty();
        bpm_->UnpinPage(first_page_id_, true);

        table_heap_ = std::make_unique<TableHeap>(bpm_.get(), first_page_id_);

        // Create test schema
        std::vector<Column> cols;
        cols.emplace_back("id", TypeId::INTEGER);
        cols.emplace_back("data", TypeId::VARCHAR, 64);
        schema_ = Schema(std::move(cols));
    }

    void TearDown() override {
        table_heap_.reset();
        bpm_.reset();
        dm_.reset();
        system("rm -rf ./test_th_data");
    }

    std::unique_ptr<DiskManager> dm_;
    std::unique_ptr<BufferPoolManager> bpm_;
    std::unique_ptr<TableHeap> table_heap_;
    uint16_t file_id_;
    page_id_t first_page_id_;
    Schema schema_;
};

TEST_F(TableHeapTest, InsertAndGet) {
    std::vector<Value> values = {Value::CreateInteger(1),
                                  Value::CreateVarchar("test_data")};
    auto tuple = Tuple::CreateFromValues(values, &schema_);

    RID rid;
    ASSERT_TRUE(table_heap_->InsertTuple(tuple, &rid));
    EXPECT_NE(rid.page_id, INVALID_PAGE_ID);
    EXPECT_GE(rid.slot_id, 0);

    Tuple retrieved;
    ASSERT_TRUE(table_heap_->GetTuple(rid, &retrieved, &schema_));
    EXPECT_EQ(retrieved.GetValue(&schema_, 0).GetAsInteger(), 1);
    EXPECT_EQ(retrieved.GetValue(&schema_, 1).GetAsVarchar(), "test_data");
}

TEST_F(TableHeapTest, MassInsertAndScan) {
    const int N = 500;
    for (int i = 0; i < N; i++) {
        std::vector<Value> values = {
            Value::CreateInteger(i),
            Value::CreateVarchar("row_" + std::to_string(i))
        };
        auto tuple = Tuple::CreateFromValues(values, &schema_);
        RID rid;
        ASSERT_TRUE(table_heap_->InsertTuple(tuple, &rid));
    }

    auto it = table_heap_->MakeIterator();
    int count = 0;
    while (it->HasNext()) {
        auto [rid, tuple] = it->Next();
        count++;
        EXPECT_NE(rid.page_id, INVALID_PAGE_ID);
    }
    EXPECT_EQ(count, N);
}
