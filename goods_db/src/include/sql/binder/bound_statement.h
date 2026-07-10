#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/rid.h"
#include "type/schema.h"
#include "type/value.h"

namespace goods_db {

// =============================================================================
// Forward declarations
// =============================================================================
class Tuple;

// =============================================================================
// BoundExpression — evaluated against actual tuples
// =============================================================================

/** Base class for all bound expressions */
class BoundExpression {
public:
    virtual ~BoundExpression() = default;

    /** Evaluate this expression against a tuple */
    virtual Value Evaluate(const Tuple* tuple, const Schema* schema) const = 0;

    /** Get the return type of this expression */
    virtual TypeId GetReturnType() const = 0;

    /** Human-readable representation */
    virtual std::string ToString() const = 0;

    /** Optional alias */
    std::string alias;
};

/** Column reference bound to a schema column index */
class BoundColumnRef : public BoundExpression {
public:
    int32_t col_idx{-1};
    TypeId column_type{TypeId::INVALID};

    BoundColumnRef() = default;
    BoundColumnRef(int32_t idx, TypeId type) : col_idx(idx), column_type(type) {}

    Value Evaluate(const Tuple* tuple, const Schema* schema) const override;
    TypeId GetReturnType() const override { return column_type; }
    std::string ToString() const override;
};

/** Constant / literal value */
class BoundConstant : public BoundExpression {
public:
    Value value;

    BoundConstant() = default;
    explicit BoundConstant(Value val) : value(std::move(val)) {}

    Value Evaluate(const Tuple* /*tuple*/, const Schema* /*schema*/) const override {
        return value;
    }
    TypeId GetReturnType() const override { return value.GetTypeId(); }
    std::string ToString() const override { return value.ToString(); }
};

/** Binary operation (arithmetic, logical, string concat) */
class BoundBinaryOp : public BoundExpression {
public:
    std::string op;  // "+", "-", "*", "/", "%", "AND", "OR", "||"
    std::unique_ptr<BoundExpression> left;
    std::unique_ptr<BoundExpression> right;
    TypeId result_type{TypeId::INVALID};

    BoundBinaryOp() = default;
    BoundBinaryOp(std::string op_name, std::unique_ptr<BoundExpression> l,
                  std::unique_ptr<BoundExpression> r)
        : op(std::move(op_name)), left(std::move(l)), right(std::move(r)) {}

    Value Evaluate(const Tuple* tuple, const Schema* schema) const override;
    TypeId GetReturnType() const override { return result_type; }
    std::string ToString() const override;
};

/** Comparison expression */
class BoundComparison : public BoundExpression {
public:
    std::string op;  // "=", "<", ">", "<=", ">=", "<>", "!="
    std::unique_ptr<BoundExpression> left;
    std::unique_ptr<BoundExpression> right;

    BoundComparison() = default;
    BoundComparison(std::string op_name, std::unique_ptr<BoundExpression> l,
                    std::unique_ptr<BoundExpression> r)
        : op(std::move(op_name)), left(std::move(l)), right(std::move(r)) {}

    Value Evaluate(const Tuple* tuple, const Schema* schema) const override;
    TypeId GetReturnType() const override { return TypeId::BOOLEAN; }
    std::string ToString() const override;
};

/** Unary operation (NOT, negation) */
class BoundUnaryOp : public BoundExpression {
public:
    std::string op;  // "NOT", "-"
    std::unique_ptr<BoundExpression> operand;
    TypeId result_type{TypeId::INVALID};

    BoundUnaryOp() = default;

    Value Evaluate(const Tuple* tuple, const Schema* schema) const override;
    TypeId GetReturnType() const override { return result_type; }
    std::string ToString() const override;
};

/** Function call (aggregate or scalar) */
class BoundFunctionCall : public BoundExpression {
public:
    std::string function_name;
    std::vector<std::unique_ptr<BoundExpression>> arguments;
    bool is_distinct{false};
    bool is_star{false};

    BoundFunctionCall() = default;

    Value Evaluate(const Tuple* tuple, const Schema* schema) const override;
    TypeId GetReturnType() const override;
    std::string ToString() const override;
};

/** SELECT * expression */
class BoundStar : public BoundExpression {
public:
    Value Evaluate(const Tuple* /*tuple*/, const Schema* /*schema*/) const override {
        return Value();  // Not evaluable directly
    }
    TypeId GetReturnType() const override { return TypeId::INVALID; }
    std::string ToString() const override { return "*"; }
};

// =============================================================================
// BoundStatement — bound (semantically checked) SQL statements
// =============================================================================

/** Base class for all bound statements */
class BoundStatement {
public:
    virtual ~BoundStatement() = default;
    virtual std::string ToString() const = 0;
};

/** Bound SELECT statement */
class BoundSelectStmt : public BoundStatement {
public:
    // SELECT clause
    std::vector<std::unique_ptr<BoundExpression>> select_list;
    bool is_distinct{false};

    // FROM clause
    std::string table_name;
    const Schema* table_schema{nullptr};

    // WHERE clause
    std::unique_ptr<BoundExpression> where_clause;

    // GROUP BY
    std::vector<std::unique_ptr<BoundExpression>> group_by;

    // HAVING
    std::unique_ptr<BoundExpression> having_clause;

    // ORDER BY
    struct OrderByItem {
        std::unique_ptr<BoundExpression> expression;
        bool is_asc{true};
    };
    std::vector<OrderByItem> order_by;

    // LIMIT / OFFSET
    int64_t limit{-1};
    int64_t offset{0};

    std::string ToString() const override;
};

/** Bound INSERT statement */
class BoundInsertStmt : public BoundStatement {
public:
    std::string table_name;
    const Schema* table_schema{nullptr};

    // Column indices for the explicit column list (empty = all columns in order)
    std::vector<int32_t> column_indices;

    // Values to insert: each row is a list of expressions
    std::vector<std::vector<std::unique_ptr<BoundExpression>>> value_rows;

    std::string ToString() const override;
};

/** Bound UPDATE statement */
class BoundUpdateStmt : public BoundStatement {
public:
    std::string table_name;
    const Schema* table_schema{nullptr};

    // SET clause: col_idx → new value expression
    struct SetClause {
        int32_t col_idx;
        std::unique_ptr<BoundExpression> value;
    };
    std::vector<SetClause> set_clauses;

    // WHERE clause
    std::unique_ptr<BoundExpression> where_clause;

    std::string ToString() const override;
};

/** Bound DELETE statement */
class BoundDeleteStmt : public BoundStatement {
public:
    std::string table_name;
    const Schema* table_schema{nullptr};

    // WHERE clause
    std::unique_ptr<BoundExpression> where_clause;

    std::string ToString() const override;
};

/** Bound CREATE TABLE statement */
class BoundCreateStmt : public BoundStatement {
public:
    std::string table_name;
    Schema schema;
    bool if_not_exists{false};

    std::string ToString() const override;
};

/** Bound DROP TABLE statement */
class BoundDropStmt : public BoundStatement {
public:
    std::string table_name;
    bool if_exists{false};

    std::string ToString() const override;
};

/** Bound CREATE INDEX statement */
class BoundCreateIndexStmt : public BoundStatement {
public:
    std::string index_name;
    std::string table_name;
    const Schema* table_schema{nullptr};
    int32_t col_idx{-1};
    std::string index_type;  // "btree" or "hash"
    bool is_unique{false};

    std::string ToString() const override;
};

}  // namespace goods_db
