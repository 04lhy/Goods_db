/**
 * goods_db Performance Benchmark Suite
 */

#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "common/logger.h"
#include "sql/goods_handler.h"
#include "storage/table/table_heap.h"
#include "common/test_common.h"
#include "type/schema.h"

using namespace goods_db;

// -----------------------------------------------------------------------------
// Benchmark harness
// -----------------------------------------------------------------------------

class BenchmarkTest : public ::testing::Test {
 protected:
  void SetUp() override {
    system("rm -rf ./bench_data");
    goods_handler::engine_init();
    bpm_ = goods_handler::GetSharedBPM();
    dm_ = goods_handler::GetSharedDiskManager();
    catalog_ = goods_handler::GetSharedCatalog();
  }

  void TearDown() override {
    goods_handler::engine_deinit();
    system("rm -rf ./bench_data");
  }

  struct BenchResult {
    double avg_us, p50_us, p95_us, p99_us;
  };

  BenchResult RunBench(std::function<void()> fn) {
    std::vector<double> times;
    times.reserve(10);
    for (int i = 0; i < 5; i++) fn();  // warmup
    for (int i = 0; i < 10; i++) {
      auto start = std::chrono::high_resolution_clock::now();
      fn();
      auto end = std::chrono::high_resolution_clock::now();
      times.push_back(
          std::chrono::duration<double, std::micro>(end - start).count());
    }
    std::sort(times.begin(), times.end());
    BenchResult r;
    r.p50_us = times[4]; r.p95_us = times[8]; r.p99_us = times[9];
    double sum = 0; for (double t : times) sum += t;
    r.avg_us = sum / times.size();
    return r;
  }

  TableHeap* CreateTableWithRows(const std::string& name, int num_rows) {
    auto schema = test::CreateTestSchema();
    goods_handler handler(bpm_, dm_, catalog_);
    handler.create(name.c_str(), &schema);
    handler.open(name.c_str());
    for (int i = 0; i < num_rows; i++) {
      auto values = test::CreateTestValues(i, "item_" + std::to_string(i), 9.99);
      Tuple tuple = Tuple::CreateFromValues(values, &schema);
      handler.write_row(tuple);
    }
    handler.close();
    return catalog_->GetTable(name)->table_heap.get();
  }

  BufferPoolManager* bpm_{nullptr};
  DiskManager* dm_{nullptr};
  Catalog* catalog_{nullptr};
};

// =============================================================================
// Benchmark 1: Full Table Scan 1K
// =============================================================================
TEST_F(BenchmarkTest, FullTableScan_1K) {
  TableHeap* heap = CreateTableWithRows("bench_scan_1k", 1000);
  ASSERT_NE(heap, nullptr);
  auto schema = test::CreateTestSchema();
  auto r = RunBench([&]() {
    auto it = heap->MakeIterator();
    int c = 0; while (it->HasNext()) { it->Next(); c++; }
    EXPECT_EQ(c, 1000);
  });
  std::cout << "[BENCH] FullTableScan 1K: avg=" << r.avg_us
            << "us P50=" << r.p50_us << "us P95=" << r.p95_us << "us\n";
  SUCCEED();
}

// =============================================================================
// Benchmark 2: Full Table Scan 10K
// =============================================================================
TEST_F(BenchmarkTest, FullTableScan_10K) {
  TableHeap* heap = CreateTableWithRows("bench_scan_10k", 10000);
  ASSERT_NE(heap, nullptr);
  auto schema = test::CreateTestSchema();
  auto r = RunBench([&]() {
    auto it = heap->MakeIterator();
    int c = 0; while (it->HasNext()) { it->Next(); c++; }
    EXPECT_EQ(c, 10000);
  });
  std::cout << "[BENCH] FullTableScan 10K: avg=" << r.avg_us
            << "us P50=" << r.p50_us << "us\n";
  SUCCEED();
}

// =============================================================================
// Benchmark 3: Insert Performance
// =============================================================================
TEST_F(BenchmarkTest, Insert_1K) {
  auto schema = test::CreateTestSchema();
  goods_handler handler(bpm_, dm_, catalog_);
  handler.create("bench_insert", &schema);
  handler.open("bench_insert");
  int counter = 1000;
  auto r = RunBench([&]() {
    auto vals = test::CreateTestValues(counter, "ins_" + std::to_string(counter), counter * 1.5);
    Tuple t = Tuple::CreateFromValues(vals, &schema);
    handler.write_row(t);
    counter++;
  });
  handler.close();
  std::cout << "[BENCH] Insert: avg=" << r.avg_us << "us P50=" << r.p50_us
            << "us P99=" << r.p99_us << "us\n";
  SUCCEED();
}

// =============================================================================
// Benchmark 4: B+Tree Index Operations
// =============================================================================
TEST_F(BenchmarkTest, BPlusTreeInsertAndLookup_1K) {
  auto schema = test::CreateTestSchema();
  goods_handler handler(bpm_, dm_, catalog_);
  handler.create("bench_bt", &schema);
  handler.open("bench_bt");
  for (int i = 0; i < 1000; i++) {
    auto vals = test::CreateTestValues(i, "bt_" + std::to_string(i), 9.99);
    handler.write_row(Tuple::CreateFromValues(vals, &schema));
  }
  handler.close();

  // Create B+Tree index
  auto key_schema = Schema(std::vector<Column>{Column("id", TypeId::INTEGER)});
  bool ok = catalog_->CreateIndex("idx_bt", "bench_bt", "id",
                                   IndexInfo::IndexType::BTREE,
                                   INVALID_PAGE_ID);
  ASSERT_TRUE(ok);

  // Point query benchmark (if index was properly created)
  IndexInfo* info = catalog_->GetIndex("idx_bt");
  if (info && info->index_instance) {
    auto r = RunBench([&]() {
      RID rid = info->index_instance->GetValue(500);
      (void)rid;
    });
    std::cout << "[BENCH] B+Tree PointQuery 1K: avg=" << r.avg_us
              << "us P50=" << r.p50_us << "us\n";
  } else {
    std::cout << "[BENCH] B+Tree PointQuery 1K: skipped (index not built)\n";
  }
  SUCCEED();
}

// =============================================================================
// Benchmark 5: Hash Index Insert + Lookup
// =============================================================================
TEST_F(BenchmarkTest, HashIndexInsertAndLookup_1K) {
  auto schema = test::CreateTestSchema();
  goods_handler handler(bpm_, dm_, catalog_);
  handler.create("bench_hi", &schema);
  handler.open("bench_hi");
  for (int i = 0; i < 1000; i++) {
    auto vals = test::CreateTestValues(i, "hi_" + std::to_string(i), 9.99);
    handler.write_row(Tuple::CreateFromValues(vals, &schema));
  }
  handler.close();

  bool ok = catalog_->CreateIndex("idx_hi", "bench_hi", "id",
                                   IndexInfo::IndexType::HASH,
                                   INVALID_PAGE_ID);
  ASSERT_TRUE(ok);

  IndexInfo* hi_info = catalog_->GetIndex("idx_hi");
  if (hi_info && hi_info->index_instance) {
    auto r = RunBench([&]() {
      RID rid = hi_info->index_instance->GetValue(500);
      (void)rid;
    });
    std::cout << "[BENCH] HashIndex PointQuery 1K: avg=" << r.avg_us
              << "us P50=" << r.p50_us << "us\n";
  } else {
    std::cout << "[BENCH] HashIndex PointQuery 1K: skipped (index not built)\n";
  }
  SUCCEED();
}

// =============================================================================
// Benchmark 6: GroupBy Aggregation
// =============================================================================
TEST_F(BenchmarkTest, GroupByAggregation_10K) {
  TableHeap* heap = CreateTableWithRows("bench_gb", 10000);
  ASSERT_NE(heap, nullptr);
  auto schema = test::CreateTestSchema();
  auto r = RunBench([&]() {
    auto it = heap->MakeIterator();
    std::map<int, int> groups;
    while (it->HasNext()) {
      auto [rid, tuple] = it->Next();
      groups[tuple.GetValue(&schema, 0).GetAsInteger() % 100]++;
    }
    EXPECT_EQ(groups.size(), 100u);
  });
  std::cout << "[BENCH] GroupBy 100 groups/10K: avg=" << r.avg_us
            << "us P50=" << r.p50_us << "us\n";
  SUCCEED();
}
