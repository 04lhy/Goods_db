#include "sql/executor/execution_engine.h"

#include "catalog/catalog.h"
#include "sql/binder/binder.h"
#include "sql/executor/abstract_executor.h"
#include "sql/goods_handler.h"
#include "sql/handler.h"
#include "sql/optimizer/optimizer.h"
#include "sql/parser/parser.h"
#include "sql/planner/planner.h"

namespace goods_db {

ExecutionEngine::ExecutionEngine(BufferPoolManager* bpm, DiskManager* dm,
                                   Catalog* catalog)
    : bpm_(bpm), dm_(dm), catalog_(catalog) {
    optimizer_ = std::make_unique<Optimizer>();
}

int ExecutionEngine::ExecuteSQL(const std::string& sql,
                                  std::vector<Tuple>* results,
                                  const Schema** output_schema) {
    last_error_.clear();

    // Step 1: Parse
    Parser parser;
    std::vector<std::unique_ptr<ASTStatement>> ast_stmts;
    try {
        ast_stmts = parser.Parse(sql);
    } catch (const std::exception& e) {
        last_error_ = std::string("Parse error: ") + e.what();
        return -1;
    }
    if (ast_stmts.empty()) {
        last_error_ = "No statements parsed";
        return -1;
    }

    // Step 2: Bind
    Binder binder(catalog_);
    auto bound = binder.Bind(ast_stmts[0].get());
    if (!bound) {
        last_error_ = binder.error_message();
        if (last_error_.empty()) last_error_ = "Binding failed";
        return -1;
    }

    // Step 3: Plan
    Planner planner;
    auto plan = planner.Plan(bound.get());
    if (!plan) {
        last_error_ = "Planning failed";
        return -1;
    }

    // Step 4: Optimize
    plan = optimizer_->Optimize(std::move(plan));
    if (!plan) {
        last_error_ = "Optimization failed";
        return -1;
    }

    // Step 5: Execute
    return ExecutePlan(plan.get(), results, output_schema);
}

int ExecutionEngine::ExecutePlan(PlanNode* plan,
                                   std::vector<Tuple>* results,
                                   const Schema** output_schema) {
    if (!plan) return -1;

    // Create handler context
    goods_handler handler(bpm_, dm_, catalog_);

    ExecutorContext ctx;
    ctx.table_handler = &handler;
    ctx.catalog = catalog_;
    ctx.bpm = bpm_;
    ctx.disk_manager = dm_;

    // Create executor tree
    auto executor = ExecutorFactory::Create(&ctx, plan);
    if (!executor) {
        last_error_ = "Executor creation failed";
        return -1;
    }

    // Initialize and execute
    executor->Init();

    if (output_schema) {
        *output_schema = executor->GetOutputSchema();
    }

    if (results) {
        Tuple tuple;
        while (executor->Next(&tuple, nullptr)) {
            results->push_back(std::move(tuple));
        }
    } else {
        // DML: execute once
        Tuple dummy;
        executor->Next(&dummy, nullptr);
    }

    return 0;
}

}  // namespace goods_db
