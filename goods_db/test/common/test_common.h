#pragma once
#include <gtest/gtest.h>
#include <memory>
#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "catalog/catalog.h"
#include "sql/goods_handler.h"
#include "sql/handlerton.h"
#include "storage/disk/disk_manager.h"
#include "type/schema.h"
#include "type/value.h"

namespace goods_db {
namespace test {

/** Create a test schema for the new retail warehouse scenario */
inline Schema CreateTestSchema() {
    std::vector<Column> cols;
    cols.emplace_back("id", TypeId::INTEGER);
    cols.emplace_back("name", TypeId::VARCHAR, 128);
    cols.emplace_back("price", TypeId::DECIMAL);
    return Schema(std::move(cols));
}

/** Create test values matching the test schema */
inline std::vector<Value> CreateTestValues(int32_t id, const std::string& name,
                                            double price) {
    return {Value::CreateInteger(id), Value::CreateVarchar(name),
            Value::CreateDecimal(price)};
}

/** Create and initialize the full engine stack */
struct TestEngine {
    std::unique_ptr<DiskManager> dm;
    std::unique_ptr<BufferPoolManager> bpm;
    std::unique_ptr<Catalog> catalog;

    TestEngine(size_t pool_size = 100) {
        dm = std::make_unique<DiskManager>();
        dm->SetDataDir("./test_data");

        auto replacer = std::make_unique<ClockReplacer>(pool_size);
        bpm = std::make_unique<BufferPoolManager>(pool_size, dm.get(),
                                                   std::move(replacer));
        catalog = std::make_unique<Catalog>(bpm.get());
    }

    ~TestEngine() {
        catalog.reset();
        bpm->FlushAllPages();
        bpm.reset();
        dm.reset();
        system("rm -rf ./test_data");
    }
};

}  // namespace test
}  // namespace goods_db
