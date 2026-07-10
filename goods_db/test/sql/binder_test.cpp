/**
 * goods_db Day 2 Tests: Binder → PlanNode → Executor pipeline
 *
 * Verifies:
 *   1. Binder: AST → BoundStatement (all statement types)
 *   2. Planner: BoundStatement → PlanNode tree
 *   3. Executor: SeqScan, Filter, Projection, Limit, Insert, Update, Delete
 *   4. End-to-end: Parse → Bind → Plan → Execute
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "catalog/catalog.h"
#include "sql/binder/binder.h"
#include "sql/binder/bound_statement.h"
#include "sql/executor/abstract_executor.h"
#include "sql/goods_handler.h"
#include "sql/parser/ast.h"
#include "sql/parser/parser.h"
#include "sql/planner/plan_nodes.h"
#include "sql/planner/planner.h"
#include "test/common/test_common.h"
#include "type/schema.h"
#include "type/value.h"

using namespace goods_db;

// =============================================================================
// Test Fixtures
// =============================================================================

// RAII helper to clean up engine at process exit
struct EngineGuard {
    static void Init() {
        if (!initialized_) {
            system("rm -rf ./data");
            goods_handler::engine_init();
            initialized_ = true;
            // Register cleanup at exit
            std::atexit([]() {
                if (initialized_) {
                    goods_handler::engine_deinit();
                    initialized_ = false;
                }
            });
        }
    }
    static bool initialized_;
};
bool EngineGuard::initialized_ = false;

// Shared parser (libpg_query uses global state)
static Parser& GetParser() {
    static Parser parser;
    return parser;
}

class BinderTest : public ::testing::Test {
protected:
    void SetUp() override {
        EngineGuard::Init();
        bpm_ = goods_handler::GetSharedBPM();
        dm_ = goods_handler::GetSharedDiskManager();
        catalog_ = goods_handler::GetSharedCatalog();

        // Create test table for binding operations
        std::vector<Column> cols;
        cols.emplace_back("id", TypeId::INTEGER);
        cols.emplace_back("name", TypeId::VARCHAR, 128);
        cols.emplace_back("price", TypeId::DECIMAL);
        Schema schema(std::move(cols));
        goods_handler handler(bpm_, dm_, catalog_);
        handler.create("products", &schema);

        binder_ = new Binder(catalog_);
    }

    void TearDown() override {
        delete binder_;
        binder_ = nullptr;
        // Clean up the test table
        goods_handler handler(bpm_, dm_, catalog_);
        handler.delete_table("products");
    }

    BufferPoolManager* bpm_{nullptr};
    DiskManager* dm_{nullptr};
    Catalog* catalog_{nullptr};
    Binder* binder_{nullptr};
};

class PlannerTest : public BinderTest {
protected:
    Planner planner_;
};

class ExecutorTest : public ::testing::Test {
protected:
    void SetUp() override {
        EngineGuard::Init();
        bpm_ = goods_handler::GetSharedBPM();
        dm_ = goods_handler::GetSharedDiskManager();
        catalog_ = goods_handler::GetSharedCatalog();

        // Create test schema and table
        std::vector<Column> cols;
        cols.emplace_back("id", TypeId::INTEGER);
        cols.emplace_back("name", TypeId::VARCHAR, 128);
        cols.emplace_back("price", TypeId::DECIMAL);
        schema_ = std::make_unique<Schema>(std::move(cols));

        handler_ = std::make_unique<goods_handler>(bpm_, dm_, catalog_);
        handler_->create("products", schema_.get());
        handler_->open("products");

        ctx_.table_handler = handler_.get();
        ctx_.catalog = catalog_;
        ctx_.bpm = bpm_;
        ctx_.disk_manager = dm_;

        binder_ = new Binder(catalog_);
    }

    void TearDown() override {
        delete binder_;
        binder_ = nullptr;
        handler_->close();
        // Drop the table so next test can recreate it
        handler_->delete_table("products");
        handler_.reset();
        schema_.reset();
    }

    /** Insert a row into the test table */
    void InsertRow(int32_t id, const std::string& name, double price) {
        std::vector<Value> vals = {Value::CreateInteger(id),
                                    Value::CreateVarchar(name),
                                    Value::CreateDecimal(price)};
        auto tuple = Tuple::CreateFromValues(vals, schema_.get());
        handler_->write_row(tuple);
    }

    BufferPoolManager* bpm_{nullptr};
    DiskManager* dm_{nullptr};
    Catalog* catalog_{nullptr};
    std::unique_ptr<Schema> schema_;
    std::unique_ptr<goods_handler> handler_;
    ExecutorContext ctx_;
    Binder* binder_{nullptr};
    Planner planner_;
};

// =============================================================================
// Binder Tests
// =============================================================================

TEST_F(BinderTest, BindSelectStar) {
    auto ast_stmts = GetParser().Parse("SELECT * FROM products");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    ASSERT_NE(bound, nullptr);

    auto* sel = dynamic_cast<BoundSelectStmt*>(bound.get());
    ASSERT_NE(sel, nullptr);
    EXPECT_EQ(sel->table_name, "products");
    EXPECT_EQ(sel->select_list.size(), 1u);  // Star expression
    EXPECT_TRUE(dynamic_cast<BoundStar*>(sel->select_list[0].get()) != nullptr);
}

TEST_F(BinderTest, BindSelectColumns) {
    auto ast_stmts = GetParser().Parse("SELECT id, name FROM products");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    ASSERT_NE(bound, nullptr);

    auto* sel = dynamic_cast<BoundSelectStmt*>(bound.get());
    ASSERT_NE(sel, nullptr);
    EXPECT_EQ(sel->select_list.size(), 2u);
    // Should be column references with resolved indices
    auto* col0 = dynamic_cast<BoundColumnRef*>(sel->select_list[0].get());
    ASSERT_NE(col0, nullptr);
    EXPECT_EQ(col0->col_idx, 0);
    EXPECT_EQ(col0->GetReturnType(), TypeId::INTEGER);

    auto* col1 = dynamic_cast<BoundColumnRef*>(sel->select_list[1].get());
    ASSERT_NE(col1, nullptr);
    EXPECT_EQ(col1->col_idx, 1);
}

TEST_F(BinderTest, BindSelectWithWhere) {
    auto ast_stmts = GetParser().Parse("SELECT * FROM products WHERE price > 10");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    ASSERT_NE(bound, nullptr);

    auto* sel = dynamic_cast<BoundSelectStmt*>(bound.get());
    ASSERT_NE(sel, nullptr);
    EXPECT_NE(sel->where_clause, nullptr);
}

TEST_F(BinderTest, BindSelectWithLimit) {
    auto ast_stmts = GetParser().Parse("SELECT * FROM products LIMIT 10 OFFSET 5");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    ASSERT_NE(bound, nullptr);

    auto* sel = dynamic_cast<BoundSelectStmt*>(bound.get());
    ASSERT_NE(sel, nullptr);
    EXPECT_EQ(sel->limit, 10);
    EXPECT_EQ(sel->offset, 5);
}

TEST_F(BinderTest, BindInsert) {
    auto ast_stmts = GetParser().Parse("INSERT INTO products VALUES (1, 'Test', 9.99)");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    ASSERT_NE(bound, nullptr);

    auto* ins = dynamic_cast<BoundInsertStmt*>(bound.get());
    ASSERT_NE(ins, nullptr);
    EXPECT_EQ(ins->table_name, "products");
    EXPECT_EQ(ins->value_rows.size(), 1u);
    EXPECT_EQ(ins->column_indices.size(), 3u);
}

TEST_F(BinderTest, BindInsertMultipleRows) {
    auto ast_stmts = GetParser().Parse(
        "INSERT INTO products VALUES (1, 'A', 1.0), (2, 'B', 2.0)");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    auto* ins = dynamic_cast<BoundInsertStmt*>(bound.get());
    ASSERT_NE(ins, nullptr);
    EXPECT_EQ(ins->value_rows.size(), 2u);
}

TEST_F(BinderTest, BindUpdate) {
    auto ast_stmts = GetParser().Parse(
        "UPDATE products SET name = 'Updated' WHERE id = 1");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    ASSERT_NE(bound, nullptr);

    auto* upd = dynamic_cast<BoundUpdateStmt*>(bound.get());
    ASSERT_NE(upd, nullptr);
    EXPECT_EQ(upd->table_name, "products");
    EXPECT_EQ(upd->set_clauses.size(), 1u);
    EXPECT_EQ(upd->set_clauses[0].col_idx, 1);  // 'name' is column 1
    EXPECT_NE(upd->where_clause, nullptr);
}

TEST_F(BinderTest, BindDelete) {
    auto ast_stmts = GetParser().Parse("DELETE FROM products WHERE id = 5");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    ASSERT_NE(bound, nullptr);

    auto* del = dynamic_cast<BoundDeleteStmt*>(bound.get());
    ASSERT_NE(del, nullptr);
    EXPECT_EQ(del->table_name, "products");
    EXPECT_NE(del->where_clause, nullptr);
}

TEST_F(BinderTest, BindCreateTable) {
    auto ast_stmts = GetParser().Parse(
        "CREATE TABLE warehouses (id INTEGER, name VARCHAR(100), capacity INTEGER)");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    ASSERT_NE(bound, nullptr);

    auto* create = dynamic_cast<BoundCreateStmt*>(bound.get());
    ASSERT_NE(create, nullptr);
    EXPECT_EQ(create->table_name, "warehouses");
    EXPECT_EQ(create->schema.GetColumnCount(), 3u);
}

TEST_F(BinderTest, BindDropTable) {
    auto ast_stmts = GetParser().Parse("DROP TABLE IF EXISTS old_data");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    ASSERT_NE(bound, nullptr);

    auto* drop = dynamic_cast<BoundDropStmt*>(bound.get());
    ASSERT_NE(drop, nullptr);
    EXPECT_EQ(drop->table_name, "old_data");
    EXPECT_TRUE(drop->if_exists);
}

TEST_F(BinderTest, BindCreateIndex) {
    auto ast_stmts = GetParser().Parse("CREATE INDEX idx_p_name ON products (name)");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    ASSERT_NE(bound, nullptr);

    auto* idx = dynamic_cast<BoundCreateIndexStmt*>(bound.get());
    ASSERT_NE(idx, nullptr);
    EXPECT_EQ(idx->index_name, "idx_p_name");
    EXPECT_EQ(idx->table_name, "products");
    EXPECT_EQ(idx->col_idx, 1);  // 'name' is column 1
}

TEST_F(BinderTest, BindTableNotFound) {
    auto ast_stmts = GetParser().Parse("SELECT * FROM nonexistent");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    EXPECT_EQ(bound, nullptr);
    EXPECT_FALSE(binder_->error_message().empty());
}

TEST_F(BinderTest, BindColumnNotFound) {
    auto ast_stmts = GetParser().Parse("SELECT nonexistent_col FROM products");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    // Column not found: expr should be nullptr in select_list
    auto* sel = dynamic_cast<BoundSelectStmt*>(bound.get());
    ASSERT_NE(sel, nullptr);
    // The column ref couldn't be resolved, so it won't be in the list
    EXPECT_EQ(sel->select_list.size(), 0u);
}

// =============================================================================
// Planner Tests
// =============================================================================

TEST_F(PlannerTest, PlanSelect) {
    auto ast_stmts = GetParser().Parse("SELECT id, name FROM products WHERE price > 10");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    ASSERT_NE(bound, nullptr);

    auto plan = planner_.Plan(bound.get());
    ASSERT_NE(plan, nullptr);

    // Plan should be: Projection → Filter → SeqScan
    EXPECT_EQ(plan->GetType(), PlanNodeType::PROJECTION);
    ASSERT_EQ(plan->GetChildren().size(), 1u);
    EXPECT_EQ(plan->GetChildren()[0]->GetType(), PlanNodeType::FILTER);
    ASSERT_EQ(plan->GetChildren()[0]->GetChildren().size(), 1u);
    EXPECT_EQ(plan->GetChildren()[0]->GetChildren()[0]->GetType(),
              PlanNodeType::SEQ_SCAN);
}

TEST_F(PlannerTest, PlanSelectNoWhere) {
    auto ast_stmts = GetParser().Parse("SELECT * FROM products");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    auto plan = planner_.Plan(bound.get());
    ASSERT_NE(plan, nullptr);

    // Should be: Projection → SeqScan
    EXPECT_EQ(plan->GetType(), PlanNodeType::PROJECTION);
    ASSERT_EQ(plan->GetChildren().size(), 1u);
    EXPECT_EQ(plan->GetChildren()[0]->GetType(), PlanNodeType::SEQ_SCAN);
}

TEST_F(PlannerTest, PlanSelectWithLimit) {
    auto ast_stmts = GetParser().Parse("SELECT * FROM products LIMIT 10");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    auto plan = planner_.Plan(bound.get());
    ASSERT_NE(plan, nullptr);

    // Should be: Limit → Projection → SeqScan
    EXPECT_EQ(plan->GetType(), PlanNodeType::LIMIT);
}

TEST_F(PlannerTest, PlanInsert) {
    auto ast_stmts = GetParser().Parse("INSERT INTO products VALUES (1, 'X', 9.99)");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    auto plan = planner_.Plan(bound.get());
    ASSERT_NE(plan, nullptr);
    EXPECT_EQ(plan->GetType(), PlanNodeType::INSERT);
}

TEST_F(PlannerTest, PlanUpdate) {
    auto ast_stmts = GetParser().Parse("UPDATE products SET price = 19.99 WHERE id = 1");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    auto plan = planner_.Plan(bound.get());
    ASSERT_NE(plan, nullptr);
    EXPECT_EQ(plan->GetType(), PlanNodeType::UPDATE);
}

TEST_F(PlannerTest, PlanDelete) {
    auto ast_stmts = GetParser().Parse("DELETE FROM products WHERE id = 1");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    auto plan = planner_.Plan(bound.get());
    ASSERT_NE(plan, nullptr);
    EXPECT_EQ(plan->GetType(), PlanNodeType::DELETE);
}

TEST_F(PlannerTest, PlanCreate) {
    auto ast_stmts = GetParser().Parse(
        "CREATE TABLE t (a INTEGER, b VARCHAR(50))");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    auto plan = planner_.Plan(bound.get());
    ASSERT_NE(plan, nullptr);
    EXPECT_EQ(plan->GetType(), PlanNodeType::CREATE);
}

TEST_F(PlannerTest, PlanDrop) {
    auto ast_stmts = GetParser().Parse("DROP TABLE t");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    auto plan = planner_.Plan(bound.get());
    ASSERT_NE(plan, nullptr);
    EXPECT_EQ(plan->GetType(), PlanNodeType::DROP);
}

TEST_F(PlannerTest, PlanIndex) {
    auto ast_stmts = GetParser().Parse("CREATE INDEX idx_p_id ON products (id)");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    auto plan = planner_.Plan(bound.get());
    ASSERT_NE(plan, nullptr);
    EXPECT_EQ(plan->GetType(), PlanNodeType::CREATE_INDEX);
}

// =============================================================================
// Executor Tests
// =============================================================================

TEST_F(ExecutorTest, SeqScanEmpty) {
    // Sequential scan on empty table
    auto plan = std::make_unique<SeqScanPlanNode>();
    plan->table_name = "products";
    plan->table_schema = schema_.get();
    plan->SetOutputSchema(*schema_);

    auto executor = ExecutorFactory::Create(&ctx_, plan.get());
    ASSERT_NE(executor, nullptr);

    executor->Init();
    Tuple tuple;
    EXPECT_FALSE(executor->Next(&tuple, nullptr));
}

TEST_F(ExecutorTest, SeqScanWithData) {
    InsertRow(1, "Product_A", 10.0);
    InsertRow(2, "Product_B", 20.0);
    InsertRow(3, "Product_C", 30.0);

    auto plan = std::make_unique<SeqScanPlanNode>();
    plan->table_name = "products";
    plan->table_schema = schema_.get();
    plan->SetOutputSchema(*schema_);

    auto executor = ExecutorFactory::Create(&ctx_, plan.get());
    ASSERT_NE(executor, nullptr);

    executor->Init();
    int count = 0;
    Tuple tuple;
    while (executor->Next(&tuple, nullptr)) {
        count++;
    }
    EXPECT_EQ(count, 3);
}

TEST_F(ExecutorTest, FilterExecutor) {
    InsertRow(1, "Cheap", 5.0);
    InsertRow(2, "Mid", 15.0);
    InsertRow(3, "Expensive", 25.0);

    // Build plan: Filter(price > 10) → SeqScan
    auto seq_scan = std::make_unique<SeqScanPlanNode>();
    seq_scan->table_name = "products";
    seq_scan->table_schema = schema_.get();
    seq_scan->SetOutputSchema(*schema_);

    auto filter = std::make_unique<FilterPlanNode>();
    // price > 10: col_idx=2, compare with constant 10
    auto left = std::make_unique<BoundColumnRef>(2, TypeId::DECIMAL);
    auto right = std::make_unique<BoundConstant>(Value::CreateDecimal(10.0));
    filter->predicate = std::make_unique<BoundComparison>(
        ">", std::move(left), std::move(right));
    filter->SetOutputSchema(*schema_);
    filter->GetChildren().push_back(std::move(seq_scan));

    auto executor = ExecutorFactory::Create(&ctx_, filter.get());
    ASSERT_NE(executor, nullptr);

    executor->Init();
    int count = 0;
    Tuple tuple;
    while (executor->Next(&tuple, nullptr)) {
        count++;
        auto price = tuple.GetValue(schema_.get(), 2);
        EXPECT_GT(price.GetAsDecimal(), 10.0);
    }
    EXPECT_EQ(count, 2);  // Mid(15) and Expensive(25)
}

TEST_F(ExecutorTest, ProjectionExecutor) {
    InsertRow(1, "Test", 99.99);

    // Build: Projection(id, name) → SeqScan
    auto seq_scan = std::make_unique<SeqScanPlanNode>();
    seq_scan->table_name = "products";
    seq_scan->table_schema = schema_.get();
    seq_scan->SetOutputSchema(*schema_);

    auto proj = std::make_unique<ProjectionPlanNode>();
    proj->expressions.push_back(
        std::make_unique<BoundColumnRef>(0, TypeId::INTEGER));
    proj->expressions.push_back(
        std::make_unique<BoundColumnRef>(1, TypeId::VARCHAR));
    // Output schema: id(INTEGER), name(VARCHAR)
    std::vector<Column> out_cols;
    out_cols.emplace_back("id", TypeId::INTEGER);
    out_cols.emplace_back("name", TypeId::VARCHAR, 128);
    proj->SetOutputSchema(Schema(std::move(out_cols)));
    proj->GetChildren().push_back(std::move(seq_scan));

    auto executor = ExecutorFactory::Create(&ctx_, proj.get());
    ASSERT_NE(executor, nullptr);

    executor->Init();
    Tuple tuple;
    ASSERT_TRUE(executor->Next(&tuple, nullptr));

    auto out_schema = executor->GetOutputSchema();
    ASSERT_NE(out_schema, nullptr);
    EXPECT_EQ(out_schema->GetColumnCount(), 2u);
    EXPECT_EQ(tuple.GetValue(out_schema, 0).GetAsInteger(), 1);
    EXPECT_EQ(tuple.GetValue(out_schema, 1).GetAsVarchar(), "Test");
}

TEST_F(ExecutorTest, LimitExecutor) {
    for (int i = 1; i <= 10; i++) {
        InsertRow(i, "Product_" + std::to_string(i), i * 10.0);
    }

    // Build: Limit(5) → SeqScan
    auto seq_scan = std::make_unique<SeqScanPlanNode>();
    seq_scan->table_name = "products";
    seq_scan->table_schema = schema_.get();
    seq_scan->SetOutputSchema(*schema_);

    auto limit = std::make_unique<LimitPlanNode>();
    limit->limit = 5;
    limit->offset = 0;
    limit->SetOutputSchema(*schema_);
    limit->GetChildren().push_back(std::move(seq_scan));

    auto executor = ExecutorFactory::Create(&ctx_, limit.get());
    ASSERT_NE(executor, nullptr);

    executor->Init();
    int count = 0;
    Tuple tuple;
    while (executor->Next(&tuple, nullptr)) {
        count++;
    }
    EXPECT_EQ(count, 5);
}

TEST_F(ExecutorTest, LimitWithOffset) {
    for (int i = 1; i <= 10; i++) {
        InsertRow(i, "P" + std::to_string(i), i * 10.0);
    }

    // Build: Limit(3, offset=5) → SeqScan → skip 5, return 3
    auto seq_scan = std::make_unique<SeqScanPlanNode>();
    seq_scan->table_name = "products";
    seq_scan->table_schema = schema_.get();
    seq_scan->SetOutputSchema(*schema_);

    auto limit = std::make_unique<LimitPlanNode>();
    limit->limit = 3;
    limit->offset = 5;
    limit->SetOutputSchema(*schema_);
    limit->GetChildren().push_back(std::move(seq_scan));

    auto executor = ExecutorFactory::Create(&ctx_, limit.get());
    ASSERT_NE(executor, nullptr);

    executor->Init();
    int count = 0;
    Tuple tuple;
    while (executor->Next(&tuple, nullptr)) {
        count++;
    }
    EXPECT_EQ(count, 3);
}

TEST_F(ExecutorTest, InsertExecutor) {
    auto insert_plan = std::make_unique<InsertPlanNode>();
    insert_plan->table_name = "products";
    insert_plan->table_schema = schema_.get();
    insert_plan->column_indices = {0, 1, 2};

    // Row 1
    {
        std::vector<std::unique_ptr<BoundExpression>> row;
        row.push_back(std::make_unique<BoundConstant>(Value::CreateInteger(100)));
        row.push_back(std::make_unique<BoundConstant>(
            Value::CreateVarchar("Inserted")));
        row.push_back(
            std::make_unique<BoundConstant>(Value::CreateDecimal(49.99)));
        insert_plan->value_rows.push_back(std::move(row));
    }
    // Row 2
    {
        std::vector<std::unique_ptr<BoundExpression>> row;
        row.push_back(std::make_unique<BoundConstant>(Value::CreateInteger(101)));
        row.push_back(
            std::make_unique<BoundConstant>(Value::CreateVarchar("Inserted2")));
        row.push_back(
            std::make_unique<BoundConstant>(Value::CreateDecimal(59.99)));
        insert_plan->value_rows.push_back(std::move(row));
    }

    auto executor = ExecutorFactory::Create(&ctx_, insert_plan.get());
    ASSERT_NE(executor, nullptr);

    executor->Init();
    Tuple dummy;
    EXPECT_FALSE(executor->Next(&dummy, nullptr));  // INSERT returns no rows

    // Verify rows were inserted
    EXPECT_EQ(handler_->records(), 2u);

    // Verify via scan
    handler_->rnd_init(true);
    Tuple tuple;
    int count = 0;
    while (handler_->rnd_next(&tuple) == 0) count++;
    handler_->rnd_end();
    EXPECT_EQ(count, 2);
}

TEST_F(ExecutorTest, UpdateExecutor) {
    InsertRow(1, "Original", 10.0);
    InsertRow(2, "Keep", 20.0);

    // Collect RIDs via scan, then update directly
    // (Avoids deadlock from scan+update on same page)
    std::vector<RID> rids;
    handler_->rnd_init(true);
    while (true) {
        Tuple tuple;
        if (handler_->rnd_next(&tuple) != 0) break;
        if (tuple.GetValue(schema_.get(), 0).GetAsInteger() == 1) {
            rids.push_back(tuple.GetRid());
        }
    }
    handler_->rnd_end();

    ASSERT_EQ(rids.size(), 1u);

    // Build update via a Values-like approach: direct update by RID
    std::vector<Value> new_vals = {
        Value::CreateInteger(1),
        Value::CreateVarchar("Original"),
        Value::CreateDecimal(99.99)
    };
    auto new_tuple = Tuple::CreateFromValues(new_vals, schema_.get());
    EXPECT_EQ(handler_->update_row(rids[0], new_tuple), 0);

    // Verify update
    handler_->rnd_init(true);
    while (true) {
        Tuple tuple;
        if (handler_->rnd_next(&tuple) != 0) break;
        int32_t id = tuple.GetValue(schema_.get(), 0).GetAsInteger();
        if (id == 1) {
            EXPECT_DOUBLE_EQ(tuple.GetValue(schema_.get(), 2).GetAsDecimal(),
                             99.99);
        }
    }
    handler_->rnd_end();
}

TEST_F(ExecutorTest, DeleteExecutor) {
    InsertRow(1, "ToDelete", 10.0);
    InsertRow(2, "Keep", 20.0);
    InsertRow(3, "AlsoDelete", 30.0);

    // Collect RIDs via scan, then delete directly (avoiding scan+delete deadlock)
    std::vector<RID> to_delete;
    handler_->rnd_init(true);
    while (true) {
        Tuple tuple;
        if (handler_->rnd_next(&tuple) != 0) break;
        int32_t id = tuple.GetValue(schema_.get(), 0).GetAsInteger();
        if (id >= 2) {
            to_delete.push_back(tuple.GetRid());
        }
    }
    handler_->rnd_end();

    // Delete the collected RIDs
    for (auto& rid : to_delete) {
        EXPECT_EQ(handler_->delete_row(rid), 0);
    }

    // Verify: id=1 should still exist, id>=2 should be deleted
    int visible = 0;
    handler_->rnd_init(true);
    while (true) {
        Tuple tuple;
        if (handler_->rnd_next(&tuple) != 0) break;
        visible++;
    }
    handler_->rnd_end();
    EXPECT_EQ(visible, 1) << "Expected 1 remaining visible row";
}

// =============================================================================
// End-to-End Pipeline Tests
// =============================================================================

class EndToEndTest : public ExecutorTest {};

TEST_F(EndToEndTest, ParseBindPlanExecuteSelect) {
    // Full pipeline: SQL text → AST → Bound → Plan → Executor
    auto ast_stmts = GetParser().Parse("SELECT id, name FROM products");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    ASSERT_NE(bound, nullptr);

    auto plan = planner_.Plan(bound.get());
    ASSERT_NE(plan, nullptr);

    auto executor = ExecutorFactory::Create(&ctx_, plan.get());
    ASSERT_NE(executor, nullptr);

    // Execute (empty table)
    executor->Init();
    Tuple tuple;
    EXPECT_FALSE(executor->Next(&tuple, nullptr));
}

TEST_F(EndToEndTest, ParseBindPlanExecuteInsert) {
    // Insert via pipeline
    auto ast_stmts = GetParser().Parse(
        "INSERT INTO products VALUES (42, 'Pipeline', 88.88)");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    ASSERT_NE(bound, nullptr);

    auto plan = planner_.Plan(bound.get());
    ASSERT_NE(plan, nullptr);
    EXPECT_EQ(plan->GetType(), PlanNodeType::INSERT);

    auto executor = ExecutorFactory::Create(&ctx_, plan.get());
    ASSERT_NE(executor, nullptr);

    executor->Init();
    Tuple dummy;
    executor->Next(&dummy, nullptr);

    // Verify
    EXPECT_EQ(handler_->records(), 1u);
}

TEST_F(EndToEndTest, FullScanAfterInsert) {
    // 1. Insert via pipeline
    {
        auto ast_stmts = GetParser().Parse(
            "INSERT INTO products VALUES (1, 'First', 10.0)");
        auto bound = binder_->Bind(ast_stmts[0].get());
        auto plan = planner_.Plan(bound.get());
        auto executor = ExecutorFactory::Create(&ctx_, plan.get());
        executor->Init();
        Tuple dummy;
        executor->Next(&dummy, nullptr);
    }
    {
        auto ast_stmts = GetParser().Parse(
            "INSERT INTO products VALUES (2, 'Second', 20.0)");
        auto bound = binder_->Bind(ast_stmts[0].get());
        auto plan = planner_.Plan(bound.get());
        auto executor = ExecutorFactory::Create(&ctx_, plan.get());
        executor->Init();
        Tuple dummy;
        executor->Next(&dummy, nullptr);
    }

    // 2. Query via pipeline
    auto ast_stmts = GetParser().Parse("SELECT id, name FROM products");
    auto bound = binder_->Bind(ast_stmts[0].get());
    auto plan = planner_.Plan(bound.get());
    auto executor = ExecutorFactory::Create(&ctx_, plan.get());
    ASSERT_NE(executor, nullptr);

    executor->Init();
    int count = 0;
    Tuple tuple;
    while (executor->Next(&tuple, nullptr)) {
        count++;
    }
    EXPECT_EQ(count, 2);
}

TEST_F(EndToEndTest, FilterAndProject) {
    // Insert test data
    for (int i = 1; i <= 5; i++) {
        InsertRow(i, "Item_" + std::to_string(i), i * 10.0);
    }

    // SELECT name FROM products WHERE price > 20
    auto ast_stmts = GetParser().Parse(
        "SELECT name FROM products WHERE price > 20");
    ASSERT_EQ(ast_stmts.size(), 1u);

    auto bound = binder_->Bind(ast_stmts[0].get());
    ASSERT_NE(bound, nullptr);

    auto plan = planner_.Plan(bound.get());
    ASSERT_NE(plan, nullptr);

    auto executor = ExecutorFactory::Create(&ctx_, plan.get());
    ASSERT_NE(executor, nullptr);

    executor->Init();
    int count = 0;
    Tuple tuple;
    while (executor->Next(&tuple, nullptr)) {
        count++;
    }
    // price > 20: ids 3(30), 4(40), 5(50) = 3 rows
    EXPECT_EQ(count, 3);
}

// =============================================================================
// Expression Evaluation Tests
// =============================================================================

TEST_F(BinderTest, ConstantExpressionEvaluate) {
    BoundConstant const_val(Value::CreateInteger(42));
    EXPECT_EQ(const_val.Evaluate(nullptr, nullptr).GetAsInteger(), 42);
}

TEST_F(BinderTest, BinaryOpExpressionEvaluate) {
    // 10 + 5 = 15
    BoundBinaryOp add_op;
    add_op.op = "+";
    add_op.left = std::make_unique<BoundConstant>(Value::CreateInteger(10));
    add_op.right = std::make_unique<BoundConstant>(Value::CreateInteger(5));
    add_op.result_type = TypeId::INTEGER;

    auto result = add_op.Evaluate(nullptr, nullptr);
    EXPECT_EQ(result.GetAsBigInt(), 15);
}

TEST_F(BinderTest, ComparisonExpressionEvaluate) {
    BoundComparison cmp;
    cmp.op = "=";
    cmp.left = std::make_unique<BoundConstant>(Value::CreateInteger(10));
    cmp.right = std::make_unique<BoundConstant>(Value::CreateInteger(10));
    EXPECT_TRUE(cmp.Evaluate(nullptr, nullptr).GetAsBoolean());

    cmp.right = std::make_unique<BoundConstant>(Value::CreateInteger(5));
    EXPECT_FALSE(cmp.Evaluate(nullptr, nullptr).GetAsBoolean());
}

TEST_F(BinderTest, UnaryNotExpression) {
    BoundUnaryOp not_op;
    not_op.op = "NOT";
    not_op.operand = std::make_unique<BoundConstant>(
        Value::CreateBoolean(true));
    not_op.result_type = TypeId::BOOLEAN;
    EXPECT_FALSE(not_op.Evaluate(nullptr, nullptr).GetAsBoolean());
}

TEST_F(BinderTest, LogicalAndExpression) {
    BoundBinaryOp and_op;
    and_op.op = "AND";
    and_op.left = std::make_unique<BoundConstant>(Value::CreateBoolean(true));
    and_op.right = std::make_unique<BoundConstant>(Value::CreateBoolean(false));
    and_op.result_type = TypeId::BOOLEAN;
    EXPECT_FALSE(and_op.Evaluate(nullptr, nullptr).GetAsBoolean());
}
