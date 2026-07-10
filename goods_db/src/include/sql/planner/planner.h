#pragma once

#include <memory>
#include <vector>

#include "sql/binder/bound_statement.h"
#include "sql/planner/plan_nodes.h"

namespace goods_db {

/**
 * Planner — converts a BoundStatement into an executable PlanNode tree.
 *
 * This is a simple rule-based planner. The optimizer (Day 4) will further
 * transform the plan tree with cost-based rewrites.
 *
 * Usage:
 *   Planner planner;
 *   auto plan = planner.Plan(bound_statement);
 */
class Planner {
public:
    Planner() = default;

    /** Convert a BoundStatement into a PlanNode tree */
    std::unique_ptr<PlanNode> Plan(BoundStatement* stmt);

private:
    // =========================================================================
    // Per-statement-type plan generators
    // =========================================================================

    std::unique_ptr<PlanNode> PlanSelect(BoundSelectStmt* stmt);
    std::unique_ptr<PlanNode> PlanInsert(BoundInsertStmt* stmt);
    std::unique_ptr<PlanNode> PlanUpdate(BoundUpdateStmt* stmt);
    std::unique_ptr<PlanNode> PlanDelete(BoundDeleteStmt* stmt);
    std::unique_ptr<PlanNode> PlanCreate(BoundCreateStmt* stmt);
    std::unique_ptr<PlanNode> PlanDrop(BoundDropStmt* stmt);
    std::unique_ptr<PlanNode> PlanCreateIndex(BoundCreateIndexStmt* stmt);

    /** Create a SeqScan plan node with the given table info */
    std::unique_ptr<SeqScanPlanNode> MakeSeqScan(const std::string& table_name,
                                                   const Schema* schema);
};

}  // namespace goods_db
