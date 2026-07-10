#include "sql/planner/planner.h"

#include <stdexcept>

namespace goods_db {

// =============================================================================
// PlanNode ToString implementations
// =============================================================================

std::string SeqScanPlanNode::ToString() const {
    return "SeqScan(" + table_name + ")";
}

std::string IndexScanPlanNode::ToString() const {
    return "IndexScan(" + table_name + " via " + index_name + ")";
}

std::string FilterPlanNode::ToString() const {
    return "Filter(" + (predicate ? predicate->ToString() : "?") + ")";
}

std::string ProjectionPlanNode::ToString() const {
    return "Projection(" + std::to_string(expressions.size()) + " cols)";
}

std::string InsertPlanNode::ToString() const {
    return "Insert(" + table_name + ", " +
           std::to_string(value_rows.size()) + " rows)";
}

std::string UpdatePlanNode::ToString() const {
    return "Update(" + table_name + ")";
}

std::string DeletePlanNode::ToString() const {
    return "Delete(" + table_name + ")";
}

std::string LimitPlanNode::ToString() const {
    return "Limit(" + std::to_string(limit) + ", offset=" +
           std::to_string(offset) + ")";
}

std::string ValuesPlanNode::ToString() const {
    return "Values(" + std::to_string(value_rows.size()) + " rows)";
}

std::string AggregationPlanNode::ToString() const {
    return "Aggregation(" + std::to_string(group_by.size()) + " groups)";
}

std::string SortPlanNode::ToString() const {
    return "Sort(" + std::to_string(order_by.size()) + " keys)";
}

std::string HashJoinPlanNode::ToString() const {
    return "HashJoin(" + (join_condition ? join_condition->ToString() : "cross") +
           ")";
}

std::string NestedLoopJoinPlanNode::ToString() const {
    return "NLJ(" + (join_condition ? join_condition->ToString() : "cross") + ")";
}

std::string CreatePlanNode::ToString() const {
    return "CreateTable(" + table_name + ")";
}

std::string DropPlanNode::ToString() const {
    return "DropTable(" + table_name + ")";
}

std::string CreateIndexPlanNode::ToString() const {
    return "CreateIndex(" + index_name + " ON " + table_name + ")";
}

std::string TopNPlanNode::ToString() const {
    return "TopN(" + std::to_string(limit) + ")";
}

// =============================================================================
// Planner Implementation
// =============================================================================

std::unique_ptr<PlanNode> Planner::Plan(BoundStatement* stmt) {
    if (!stmt) return nullptr;

    // Dispatch based on dynamic type
    if (auto* s = dynamic_cast<BoundSelectStmt*>(stmt)) return PlanSelect(s);
    if (auto* i = dynamic_cast<BoundInsertStmt*>(stmt)) return PlanInsert(i);
    if (auto* u = dynamic_cast<BoundUpdateStmt*>(stmt)) return PlanUpdate(u);
    if (auto* d = dynamic_cast<BoundDeleteStmt*>(stmt)) return PlanDelete(d);
    if (auto* c = dynamic_cast<BoundCreateStmt*>(stmt)) return PlanCreate(c);
    if (auto* d2 = dynamic_cast<BoundDropStmt*>(stmt)) return PlanDrop(d2);
    if (auto* c2 = dynamic_cast<BoundCreateIndexStmt*>(stmt))
        return PlanCreateIndex(c2);

    return nullptr;
}

std::unique_ptr<SeqScanPlanNode> Planner::MakeSeqScan(
    const std::string& table_name, const Schema* schema) {
    auto node = std::make_unique<SeqScanPlanNode>();
    node->table_name = table_name;
    node->table_schema = schema;
    if (schema) node->SetOutputSchema(*schema);
    return node;
}

// =============================================================================
// PlanSelect: SeqScan → Filter → Projection → (Sort →) Limit
// =============================================================================

std::unique_ptr<PlanNode> Planner::PlanSelect(BoundSelectStmt* stmt) {
    std::unique_ptr<PlanNode> root;

    // 1. SeqScan
    auto seq_scan = MakeSeqScan(stmt->table_name, stmt->table_schema);
    root = std::move(seq_scan);

    // 2. Filter (WHERE)
    if (stmt->where_clause) {
        auto filter = std::make_unique<FilterPlanNode>();
        filter->predicate = std::move(stmt->where_clause);
        filter->SetOutputSchema(stmt->table_schema ? *stmt->table_schema
                                                     : Schema());
        filter->GetChildren().push_back(std::move(root));
        root = std::move(filter);
    }

    // 3. Projection (SELECT list)
    auto proj = std::make_unique<ProjectionPlanNode>();
    // If all expressions are BoundStar, expand to all columns
    bool has_star = false;
    for (auto& expr : stmt->select_list) {
        if (dynamic_cast<BoundStar*>(expr.get())) {
            has_star = true;
            break;
        }
    }

    if (has_star && stmt->select_list.size() == 1 && stmt->table_schema) {
        // SELECT * → project all columns
        const auto& cols = stmt->table_schema->GetColumns();
        for (uint32_t i = 0; i < cols.size(); i++) {
            auto col_ref = std::make_unique<BoundColumnRef>(
                static_cast<int32_t>(i), cols[i].column_type);
            col_ref->alias = cols[i].column_name;
            proj->expressions.push_back(std::move(col_ref));
        }
    } else {
        for (auto& expr : stmt->select_list) {
            proj->expressions.push_back(std::move(expr));
        }
    }

    // Build output schema for projection
    if (stmt->table_schema) {
        std::vector<Column> out_cols;
        for (auto& expr : proj->expressions) {
            Column col;
            col.column_name = expr->alias.empty()
                                  ? expr->ToString()
                                  : expr->alias;
            col.column_type = expr->GetReturnType();
            if (col.column_type == TypeId::INVALID) {
                col.column_type = TypeId::VARCHAR;  // fallback
            }
            col.max_length = (col.column_type == TypeId::VARCHAR) ? 256 : 0;
            out_cols.push_back(std::move(col));
        }
        proj->SetOutputSchema(Schema(std::move(out_cols)));
    }

    proj->GetChildren().push_back(std::move(root));
    root = std::move(proj);

    // 4. Sort (ORDER BY) — for now, just acknowledge it
    // Full sort executor is Day 3

    // 5. Limit
    if (stmt->limit >= 0 || stmt->offset > 0) {
        auto limit_node = std::make_unique<LimitPlanNode>();
        limit_node->limit = stmt->limit;
        limit_node->offset = stmt->offset;
        limit_node->SetOutputSchema(root->GetOutputSchema());
        limit_node->GetChildren().push_back(std::move(root));
        root = std::move(limit_node);
    }

    return root;
}

// =============================================================================
// PlanInsert: Values → Insert
// =============================================================================

std::unique_ptr<PlanNode> Planner::PlanInsert(BoundInsertStmt* stmt) {
    auto insert = std::make_unique<InsertPlanNode>();
    insert->table_name = stmt->table_name;
    insert->table_schema = stmt->table_schema;
    insert->column_indices = std::move(stmt->column_indices);
    insert->value_rows = std::move(stmt->value_rows);
    return insert;
}

// =============================================================================
// PlanUpdate: SeqScan → Filter → Update
// =============================================================================

std::unique_ptr<PlanNode> Planner::PlanUpdate(BoundUpdateStmt* stmt) {
    // 1. SeqScan
    auto seq_scan = MakeSeqScan(stmt->table_name, stmt->table_schema);
    std::unique_ptr<PlanNode> root = std::move(seq_scan);

    // 2. Filter (WHERE)
    if (stmt->where_clause) {
        auto filter = std::make_unique<FilterPlanNode>();
        filter->predicate = std::move(stmt->where_clause);
        filter->SetOutputSchema(stmt->table_schema ? *stmt->table_schema
                                                     : Schema());
        filter->GetChildren().push_back(std::move(root));
        root = std::move(filter);
    }

    // 3. Update
    auto update = std::make_unique<UpdatePlanNode>();
    update->table_name = stmt->table_name;
    update->table_schema = stmt->table_schema;
    for (auto& sc : stmt->set_clauses) {
        UpdatePlanNode::SetClause usc;
        usc.col_idx = sc.col_idx;
        usc.value = std::move(sc.value);
        update->set_clauses.push_back(std::move(usc));
    }
    update->GetChildren().push_back(std::move(root));
    return update;
}

// =============================================================================
// PlanDelete: SeqScan → Filter → Delete
// =============================================================================

std::unique_ptr<PlanNode> Planner::PlanDelete(BoundDeleteStmt* stmt) {
    // 1. SeqScan
    auto seq_scan = MakeSeqScan(stmt->table_name, stmt->table_schema);
    std::unique_ptr<PlanNode> root = std::move(seq_scan);

    // 2. Filter (WHERE)
    if (stmt->where_clause) {
        auto filter = std::make_unique<FilterPlanNode>();
        filter->predicate = std::move(stmt->where_clause);
        filter->SetOutputSchema(stmt->table_schema ? *stmt->table_schema
                                                     : Schema());
        filter->GetChildren().push_back(std::move(root));
        root = std::move(filter);
    }

    // 3. Delete
    auto del = std::make_unique<DeletePlanNode>();
    del->table_name = stmt->table_name;
    del->table_schema = stmt->table_schema;
    del->GetChildren().push_back(std::move(root));
    return del;
}

// =============================================================================
// PlanCreate
// =============================================================================

std::unique_ptr<PlanNode> Planner::PlanCreate(BoundCreateStmt* stmt) {
    auto create = std::make_unique<CreatePlanNode>();
    create->table_name = stmt->table_name;
    create->schema = stmt->schema;
    create->if_not_exists = stmt->if_not_exists;
    return create;
}

// =============================================================================
// PlanDrop
// =============================================================================

std::unique_ptr<PlanNode> Planner::PlanDrop(BoundDropStmt* stmt) {
    auto drop = std::make_unique<DropPlanNode>();
    drop->table_name = stmt->table_name;
    drop->if_exists = stmt->if_exists;
    return drop;
}

// =============================================================================
// PlanCreateIndex
// =============================================================================

std::unique_ptr<PlanNode> Planner::PlanCreateIndex(BoundCreateIndexStmt* stmt) {
    auto idx = std::make_unique<CreateIndexPlanNode>();
    idx->index_name = stmt->index_name;
    idx->table_name = stmt->table_name;
    idx->table_schema = stmt->table_schema;
    idx->col_idx = stmt->col_idx;
    idx->index_type = stmt->index_type;
    idx->is_unique = stmt->is_unique;
    return idx;
}

}  // namespace goods_db
