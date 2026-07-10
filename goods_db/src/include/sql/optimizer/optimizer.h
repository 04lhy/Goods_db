#pragma once

#include <memory>
#include <string>
#include <vector>

#include "sql/planner/plan_nodes.h"

namespace goods_db {

/**
 * Optimizer — applies rewrite rules to improve plan efficiency.
 *
 * 6 optimization rules:
 *   1. Predicate pushdown: Filter → SeqScan → SeqScan(with filter)
 *   2. Column pruning: remove unnecessary Projection columns
 *   3. SeqScan → IndexScan: when an index exists on filter column
 *   4. NLJ → HashJoin: nested loop join to hash join
 *   5. NLJ → IndexJoin: when inner table has index on join column
 *   6. Sort+Limit → TopN: combine sort and limit into top-N
 */
class Optimizer {
public:
    Optimizer() = default;

    /** Apply all optimization rules to a plan tree */
    std::unique_ptr<PlanNode> Optimize(std::unique_ptr<PlanNode> plan);

    // Individual rules (exposed for testing)
    std::unique_ptr<PlanNode> Rule_PredicatePushdown(std::unique_ptr<PlanNode> plan);
    std::unique_ptr<PlanNode> Rule_ColumnPruning(std::unique_ptr<PlanNode> plan);
    std::unique_ptr<PlanNode> Rule_SeqScanToIndexScan(std::unique_ptr<PlanNode> plan);
    std::unique_ptr<PlanNode> Rule_NLJToHashJoin(std::unique_ptr<PlanNode> plan);
    std::unique_ptr<PlanNode> Rule_NLJToIndexJoin(std::unique_ptr<PlanNode> plan);
    std::unique_ptr<PlanNode> Rule_SortLimitToTopN(std::unique_ptr<PlanNode> plan);

private:
    /** Recursively apply a rule to all children */
    void ApplyToChildren(std::unique_ptr<PlanNode>& plan,
                          std::unique_ptr<PlanNode> (Optimizer::*rule)(
                              std::unique_ptr<PlanNode>));
};

/**
 * EXPLAIN — formats a plan tree for display.
 */
class Explain {
public:
    /** Generate indented plan tree string */
    static std::string ExplainPlan(PlanNode* plan);

    /** Generate plan tree with execution statistics */
    static std::string ExplainAnalyze(PlanNode* plan);

private:
    static void FormatNode(PlanNode* node, int indent, std::string& output);
};

}  // namespace goods_db
