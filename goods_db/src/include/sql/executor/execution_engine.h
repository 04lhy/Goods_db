#pragma once

#include <memory>
#include <string>
#include <vector>

#include "sql/executor/abstract_executor.h"
#include "storage/table/tuple.h"
#include "type/schema.h"

namespace goods_db {

// Forward declarations
class Catalog;
class BufferPoolManager;
class DiskManager;
class PlanNode;

/**
 * ExecutionEngine — unified entry point for SQL execution.
 *
 * Provides the complete pipeline:
 *   SQL text → Parse → Bind → Plan → Optimize → Execute
 *
 * Usage:
 *   ExecutionEngine engine(bpm, dm, catalog);
 *   std::vector<Tuple> results;
 *   const Schema* output_schema;
 *   engine.ExecuteSQL("SELECT * FROM t WHERE id = 1", &results, &output_schema);
 */
class ExecutionEngine {
public:
    ExecutionEngine(BufferPoolManager* bpm, DiskManager* dm, Catalog* catalog);

    /**
     * Execute a SQL string end-to-end.
     * @param sql The SQL text to execute
     * @param results [out] Result tuples (for SELECT queries)
     * @param output_schema [out] Schema of result tuples
     * @return 0 on success, -1 on error
     */
    int ExecuteSQL(const std::string& sql, std::vector<Tuple>* results,
                   const Schema** output_schema);

    /**
     * Execute a pre-built plan tree.
     */
    int ExecutePlan(PlanNode* plan, std::vector<Tuple>* results,
                    const Schema** output_schema);

    /** Get the last error message */
    const std::string& GetLastError() const { return last_error_; }

    /** Get the optimizer for standalone use */
    class Optimizer* GetOptimizer() { return optimizer_.get(); }

private:
    BufferPoolManager* bpm_;
    DiskManager* dm_;
    Catalog* catalog_;
    std::string last_error_;
    std::unique_ptr<class Optimizer> optimizer_;
};

}  // namespace goods_db
