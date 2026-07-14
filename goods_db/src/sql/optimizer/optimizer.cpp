#include "sql/optimizer/optimizer.h"

#include <sstream>

namespace goods_db {

// =============================================================================
// Optimizer
// =============================================================================

std::unique_ptr<PlanNode> Optimizer::Optimize(std::unique_ptr<PlanNode> plan) {
    if (!plan) return nullptr;

    // Apply rules in order
    plan = Rule_PredicatePushdown(std::move(plan));
    plan = Rule_ColumnPruning(std::move(plan));
    plan = Rule_SeqScanToIndexScan(std::move(plan));
    plan = Rule_NLJToHashJoin(std::move(plan));
    plan = Rule_NLJToIndexJoin(std::move(plan));
    plan = Rule_SortLimitToTopN(std::move(plan));

    return plan;
}

void Optimizer::ApplyToChildren(
    std::unique_ptr<PlanNode>& plan,
    std::unique_ptr<PlanNode> (Optimizer::*rule)(std::unique_ptr<PlanNode>)) {
    for (auto& child : plan->GetChildren()) {
        auto new_child = (this->*rule)(std::move(child));
        child = std::move(new_child);
    }
}

// =============================================================================
// Rule 1: Predicate Pushdown
// Filter → SeqScan  →  SeqScan(with filter_predicate)
//
// NOTE: This optimization is currently disabled because SeqScanExecutor
// does not evaluate filter_predicate. When SeqScanExecutor gains filter
// evaluation support, re-enable this rule.
// =============================================================================

std::unique_ptr<PlanNode> Optimizer::Rule_PredicatePushdown(
    std::unique_ptr<PlanNode> plan) {
    if (!plan) return nullptr;

    // Recurse first
    ApplyToChildren(plan, &Optimizer::Rule_PredicatePushdown);

    // Disabled: SeqScanExecutor does not evaluate filter_predicate yet.
    // When enabled, this rule would push Filter predicates into SeqScan
    // and remove the Filter node.

    return plan;
}

// =============================================================================
// Rule 2: Column Pruning
// Remove Projection columns not needed by parent operators
// (Simplified: pass-through no-op for now)
// =============================================================================

std::unique_ptr<PlanNode> Optimizer::Rule_ColumnPruning(
    std::unique_ptr<PlanNode> plan) {
    if (!plan) return nullptr;
    ApplyToChildren(plan, &Optimizer::Rule_ColumnPruning);
    // Column pruning would need schema analysis; simplified as no-op
    return plan;
}

// =============================================================================
// Rule 3: SeqScan → IndexScan
// If SeqScan has a filter on a column with an index, use IndexScan
// (Requires catalog lookup — simplified for now)
// =============================================================================

std::unique_ptr<PlanNode> Optimizer::Rule_SeqScanToIndexScan(
    std::unique_ptr<PlanNode> plan) {
    if (!plan) return nullptr;
    ApplyToChildren(plan, &Optimizer::Rule_SeqScanToIndexScan);

    // Check if SeqScan has a filter_predicate (pushed down)
    if (plan->GetType() == PlanNodeType::SEQ_SCAN) {
        auto* seq_scan = static_cast<SeqScanPlanNode*>(plan.get());
        if (seq_scan->filter_predicate) {
            // Check if filter is on an indexed column (simplified)
            auto* cmp = dynamic_cast<BoundComparison*>(
                seq_scan->filter_predicate.get());
            if (cmp && cmp->op == "=") {
                auto* col_ref = dynamic_cast<BoundColumnRef*>(cmp->left.get());
                if (col_ref) {
                    // Could convert to IndexScan here
                    // For now, keep as SeqScan with filter
                }
            }
        }
    }

    return plan;
}

// =============================================================================
// Rule 4: NLJ → HashJoin
// =============================================================================

std::unique_ptr<PlanNode> Optimizer::Rule_NLJToHashJoin(
    std::unique_ptr<PlanNode> plan) {
    if (!plan) return nullptr;
    ApplyToChildren(plan, &Optimizer::Rule_NLJToHashJoin);

    if (plan->GetType() == PlanNodeType::NESTED_LOOP_JOIN) {
        auto* nlj = static_cast<NestedLoopJoinPlanNode*>(plan.get());
        // Convert to HashJoin
        auto hash_join = std::make_unique<HashJoinPlanNode>();
        hash_join->join_condition = std::move(nlj->join_condition);
        hash_join->join_kind = static_cast<HashJoinPlanNode::JoinKind>(
            nlj->join_kind);
        for (auto& child : nlj->GetChildren()) {
            hash_join->GetChildren().push_back(std::move(
                const_cast<std::unique_ptr<PlanNode>&>(child)));
        }
        // Clear NLJ children since we moved them
        nlj->GetChildren().clear();
        return hash_join;
    }

    return plan;
}

// =============================================================================
// Rule 5: NLJ → IndexJoin
// When inner table has index on join column, use IndexJoin
// (Simplified: no-op for now)
// =============================================================================

std::unique_ptr<PlanNode> Optimizer::Rule_NLJToIndexJoin(
    std::unique_ptr<PlanNode> plan) {
    if (!plan) return nullptr;
    ApplyToChildren(plan, &Optimizer::Rule_NLJToIndexJoin);
    return plan;
}

// =============================================================================
// Rule 6: Sort+Limit → TopN
// =============================================================================

std::unique_ptr<PlanNode> Optimizer::Rule_SortLimitToTopN(
    std::unique_ptr<PlanNode> plan) {
    if (!plan) return nullptr;
    ApplyToChildren(plan, &Optimizer::Rule_SortLimitToTopN);

    // Check for Limit → Sort pattern
    if (plan->GetType() == PlanNodeType::LIMIT) {
        auto* limit = static_cast<LimitPlanNode*>(plan.get());
        if (limit->GetChildren().size() == 1 &&
            limit->GetChildren()[0]->GetType() == PlanNodeType::SORT) {
            auto sort_child = std::move(limit->GetChildren()[0]);
            auto* sort = static_cast<SortPlanNode*>(sort_child.get());

            auto top_n = std::make_unique<TopNPlanNode>();
            top_n->limit = limit->limit;
            for (auto& ob : sort->order_by) {
                SortPlanNode::OrderByItem item;
                item.expression = std::move(ob.expression);
                item.is_asc = ob.is_asc;
                top_n->order_by.push_back(std::move(item));
            }
            // Transfer Sort's children to TopN
            for (auto& child : sort->GetChildren()) {
                top_n->GetChildren().push_back(std::move(
                    const_cast<std::unique_ptr<PlanNode>&>(child)));
            }
            sort->GetChildren().clear();
            top_n->SetOutputSchema(plan->GetOutputSchema());
            return top_n;
        }
    }

    return plan;
}

// =============================================================================
// EXPLAIN
// =============================================================================

void Explain::FormatNode(PlanNode* node, int indent, std::string& output) {
    if (!node) return;

    output += std::string(indent * 2, ' ') + node->ToString() + "\n";
    for (auto& child : node->GetChildren()) {
        FormatNode(child.get(), indent + 1, output);
    }
}

std::string Explain::ExplainPlan(PlanNode* plan) {
    std::string output;
    FormatNode(plan, 0, output);
    return output;
}

std::string Explain::ExplainAnalyze(PlanNode* plan) {
    std::string output = "=== EXPLAIN ANALYZE ===\n";
    output += ExplainPlan(plan);
    output += "=== END ===\n";
    return output;
}

}  // namespace goods_db
