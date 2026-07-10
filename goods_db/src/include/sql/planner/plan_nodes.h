#pragma once

#include <memory>
#include <string>
#include <vector>

#include "sql/binder/bound_statement.h"
#include "type/schema.h"

namespace goods_db {

// =============================================================================
// PlanNodeType enumeration
// =============================================================================

enum class PlanNodeType : uint8_t {
    SEQ_SCAN = 0,
    INDEX_SCAN = 1,
    FILTER = 2,
    PROJECTION = 3,
    INSERT = 4,
    UPDATE = 5,
    DELETE = 6,
    HASH_JOIN = 7,
    NESTED_LOOP_JOIN = 8,
    INDEX_JOIN = 9,
    AGGREGATION = 10,
    SORT = 11,
    LIMIT = 12,
    VALUES = 13,
    CREATE = 14,
    DROP = 15,
    CREATE_INDEX = 16,
    TOP_N = 17,
};

// =============================================================================
// PlanNode — base class for all plan nodes
// =============================================================================

class PlanNode {
public:
    explicit PlanNode(PlanNodeType type) : type_(type) {}
    virtual ~PlanNode() = default;

    PlanNodeType GetType() const { return type_; }
    Schema& GetOutputSchema() { return output_schema_; }
    const Schema& GetOutputSchema() const { return output_schema_; }
    void SetOutputSchema(const Schema& schema) { output_schema_ = schema; }

    std::vector<std::unique_ptr<PlanNode>>& GetChildren() { return children_; }
    const std::vector<std::unique_ptr<PlanNode>>& GetChildren() const {
        return children_;
    }

    virtual std::string ToString() const { return "PlanNode"; }

protected:
    PlanNodeType type_;
    Schema output_schema_;
    std::vector<std::unique_ptr<PlanNode>> children_;
};

// =============================================================================
// SeqScanPlanNode — full table sequential scan
// =============================================================================

class SeqScanPlanNode : public PlanNode {
public:
    SeqScanPlanNode() : PlanNode(PlanNodeType::SEQ_SCAN) {}

    std::string table_name;
    const Schema* table_schema{nullptr};

    // Optional: filter predicate pushed down (set by optimizer)
    std::unique_ptr<BoundExpression> filter_predicate;

    std::string ToString() const override;
};

// =============================================================================
// IndexScanPlanNode — index-based scan
// =============================================================================

class IndexScanPlanNode : public PlanNode {
public:
    IndexScanPlanNode() : PlanNode(PlanNodeType::INDEX_SCAN) {}

    std::string table_name;
    const Schema* table_schema{nullptr};
    std::string index_name;
    int32_t col_idx{-1};

    // Range scan bounds
    std::unique_ptr<BoundExpression> start_key;
    std::unique_ptr<BoundExpression> end_key;

    std::string ToString() const override;
};

// =============================================================================
// FilterPlanNode — predicate filter
// =============================================================================

class FilterPlanNode : public PlanNode {
public:
    FilterPlanNode() : PlanNode(PlanNodeType::FILTER) {}

    std::unique_ptr<BoundExpression> predicate;

    std::string ToString() const override;
};

// =============================================================================
// ProjectionPlanNode — column projection
// =============================================================================

class ProjectionPlanNode : public PlanNode {
public:
    ProjectionPlanNode() : PlanNode(PlanNodeType::PROJECTION) {}

    std::vector<std::unique_ptr<BoundExpression>> expressions;

    std::string ToString() const override;
};

// =============================================================================
// InsertPlanNode — row insertion
// =============================================================================

class InsertPlanNode : public PlanNode {
public:
    InsertPlanNode() : PlanNode(PlanNodeType::INSERT) {}

    std::string table_name;
    const Schema* table_schema{nullptr};
    std::vector<int32_t> column_indices;
    std::vector<std::vector<std::unique_ptr<BoundExpression>>> value_rows;

    std::string ToString() const override;
};

// =============================================================================
// UpdatePlanNode — row update
// =============================================================================

class UpdatePlanNode : public PlanNode {
public:
    UpdatePlanNode() : PlanNode(PlanNodeType::UPDATE) {}

    std::string table_name;
    const Schema* table_schema{nullptr};

    struct SetClause {
        int32_t col_idx;
        std::unique_ptr<BoundExpression> value;
    };
    std::vector<SetClause> set_clauses;

    std::string ToString() const override;
};

// =============================================================================
// DeletePlanNode — row deletion
// =============================================================================

class DeletePlanNode : public PlanNode {
public:
    DeletePlanNode() : PlanNode(PlanNodeType::DELETE) {}

    std::string table_name;
    const Schema* table_schema{nullptr};

    std::string ToString() const override;
};

// =============================================================================
// LimitPlanNode — limit/offset
// =============================================================================

class LimitPlanNode : public PlanNode {
public:
    LimitPlanNode() : PlanNode(PlanNodeType::LIMIT) {}

    int64_t limit{-1};
    int64_t offset{0};

    std::string ToString() const override;
};

// =============================================================================
// ValuesPlanNode — literal value rows (for INSERT ... VALUES)
// =============================================================================

class ValuesPlanNode : public PlanNode {
public:
    ValuesPlanNode() : PlanNode(PlanNodeType::VALUES) {}

    std::vector<std::vector<std::unique_ptr<BoundExpression>>> value_rows;

    std::string ToString() const override;
};

// =============================================================================
// AggregationPlanNode — GROUP BY + aggregate functions
// =============================================================================

class AggregationPlanNode : public PlanNode {
public:
    AggregationPlanNode() : PlanNode(PlanNodeType::AGGREGATION) {}

    std::vector<std::unique_ptr<BoundExpression>> group_by;
    std::vector<std::unique_ptr<BoundExpression>> aggregates;

    std::string ToString() const override;
};

// =============================================================================
// SortPlanNode — ORDER BY
// =============================================================================

class SortPlanNode : public PlanNode {
public:
    SortPlanNode() : PlanNode(PlanNodeType::SORT) {}

    struct OrderByItem {
        std::unique_ptr<BoundExpression> expression;
        bool is_asc{true};
    };
    std::vector<OrderByItem> order_by;

    std::string ToString() const override;
};

// =============================================================================
// HashJoinPlanNode — hash join
// =============================================================================

class HashJoinPlanNode : public PlanNode {
public:
    HashJoinPlanNode() : PlanNode(PlanNodeType::HASH_JOIN) {}

    std::unique_ptr<BoundExpression> join_condition;
    // Join type from original AST
    enum class JoinKind { INNER, LEFT, RIGHT };
    JoinKind join_kind{JoinKind::INNER};

    // Build-side schema (left child) and probe-side schema (right child)
    const Schema* left_schema{nullptr};
    const Schema* right_schema{nullptr};

    std::string ToString() const override;
};

// =============================================================================
// NestedLoopJoinPlanNode — nested loop join
// =============================================================================

class NestedLoopJoinPlanNode : public PlanNode {
public:
    NestedLoopJoinPlanNode() : PlanNode(PlanNodeType::NESTED_LOOP_JOIN) {}

    std::unique_ptr<BoundExpression> join_condition;
    HashJoinPlanNode::JoinKind join_kind{HashJoinPlanNode::JoinKind::INNER};

    std::string ToString() const override;
};

// =============================================================================
// CreatePlanNode — CREATE TABLE
// =============================================================================

class CreatePlanNode : public PlanNode {
public:
    CreatePlanNode() : PlanNode(PlanNodeType::CREATE) {}

    std::string table_name;
    Schema schema;
    bool if_not_exists{false};

    std::string ToString() const override;
};

// =============================================================================
// DropPlanNode — DROP TABLE
// =============================================================================

class DropPlanNode : public PlanNode {
public:
    DropPlanNode() : PlanNode(PlanNodeType::DROP) {}

    std::string table_name;
    bool if_exists{false};

    std::string ToString() const override;
};

// =============================================================================
// CreateIndexPlanNode — CREATE INDEX
// =============================================================================

class CreateIndexPlanNode : public PlanNode {
public:
    CreateIndexPlanNode() : PlanNode(PlanNodeType::CREATE_INDEX) {}

    std::string index_name;
    std::string table_name;
    const Schema* table_schema{nullptr};
    int32_t col_idx{-1};
    std::string index_type;
    bool is_unique{false};

    std::string ToString() const override;
};

// =============================================================================
// TopNPlanNode — Sort + Limit combined
// =============================================================================

class TopNPlanNode : public PlanNode {
public:
    TopNPlanNode() : PlanNode(PlanNodeType::TOP_N) {}

    std::vector<SortPlanNode::OrderByItem> order_by;
    int64_t limit{-1};

    std::string ToString() const override;
};

}  // namespace goods_db
