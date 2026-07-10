#pragma once

#include <memory>
#include <string>
#include <vector>

#include "type/value.h"

namespace goods_db {

// =============================================================================
// AST Node Type Enumeration
// =============================================================================
enum class ASTNodeType : uint8_t {
    // Statements
    STATEMENT_SELECT = 0,
    STATEMENT_INSERT = 1,
    STATEMENT_UPDATE = 2,
    STATEMENT_DELETE = 3,
    STATEMENT_CREATE = 4,
    STATEMENT_DROP = 5,
    STATEMENT_CREATE_INDEX = 6,

    // Expressions
    EXPRESSION_COLUMN_REF = 10,
    EXPRESSION_CONSTANT = 11,
    EXPRESSION_BINARY_OP = 12,
    EXPRESSION_UNARY_OP = 13,
    EXPRESSION_FUNCTION_CALL = 14,
    EXPRESSION_COMPARISON = 15,
    EXPRESSION_STAR = 16,
    EXPRESSION_ALIAS = 17,
    EXPRESSION_TYPE_CAST = 18,
    EXPRESSION_SUBQUERY = 19,
    EXPRESSION_CASE = 20,
    EXPRESSION_IN_LIST = 21,
    EXPRESSION_IS_NULL = 22,
};

// =============================================================================
// Forward Declarations
// =============================================================================
class ASTExpression;  // base for all expressions
class ASTStatement;   // base for all statements

// =============================================================================
// DataType — SQL type mapping for AST
// =============================================================================
struct DataType {
    enum class Kind : uint8_t {
        INVALID = 0,
        BOOLEAN = 1,
        TINYINT = 2,
        SMALLINT = 3,
        INTEGER = 4,
        BIGINT = 5,
        DECIMAL = 6,
        REAL = 7,
        VARCHAR = 8,
        TEXT = 9,
        TIMESTAMP = 10,
        DATE = 11,
    };

    Kind kind{Kind::INVALID};
    uint32_t length{0};  // for VARCHAR(n)

    DataType() = default;
    explicit DataType(Kind k, uint32_t len = 0) : kind(k), length(len) {}

    /** Map AST DataType to internal TypeId */
    TypeId ToTypeId() const;

    /** Get SQL type name */
    std::string ToString() const;

    /** Parse from SQL type name string */
    static DataType FromString(const std::string& name, uint32_t length = 0);

    bool operator==(const DataType& other) const {
        return kind == other.kind && length == other.length;
    }
};

// =============================================================================
// ColumnDefinition — for CREATE TABLE
// =============================================================================
struct ColumnDefinition {
    std::string column_name;
    DataType data_type;
    bool is_nullable{true};
    bool is_primary_key{false};
    bool is_unique{false};
    std::string default_value;

    std::string ToString() const;
};

// =============================================================================
// OrderByClause — ORDER BY item
// =============================================================================
enum class OrderType : uint8_t { ASC = 0, DESC = 1, DEFAULT = 2 };

struct OrderByClause {
    std::unique_ptr<ASTExpression> expression;
    OrderType order_type{OrderType::DEFAULT};

    OrderByClause() = default;
    OrderByClause(std::unique_ptr<ASTExpression> expr, OrderType type)
        : expression(std::move(expr)), order_type(type) {}

    std::string ToString() const;
};

// =============================================================================
// JoinClause — JOIN information
// =============================================================================
enum class JoinType : uint8_t {
    INNER = 0,
    LEFT = 1,
    RIGHT = 2,
    FULL = 3,
    CROSS = 4,
    NATURAL = 5,
};

struct JoinClause {
    JoinType join_type{JoinType::INNER};
    std::string table_name;
    std::string alias;
    std::unique_ptr<ASTExpression> condition;  // ON clause
    std::vector<std::string> using_columns;     // USING clause

    std::string ToString() const;
};

// =============================================================================
// ASTNode — base class for all AST nodes
// =============================================================================
class ASTNode {
public:
    virtual ~ASTNode() = default;

    virtual ASTNodeType GetType() const = 0;
    virtual std::string ToString() const = 0;
};

// =============================================================================
// ASTExpression — base class for all expressions
// =============================================================================
class ASTExpression : public ASTNode {
public:
    ~ASTExpression() override = default;

    /** Optional alias for this expression (e.g., "col AS alias") */
    std::string alias;
};

// =============================================================================
// Expression Nodes
// =============================================================================

/** Column reference: table.column or just column */
class ColumnRefExpression : public ASTExpression {
public:
    std::string table_name;   // optional: empty for unqualified
    std::string column_name;  // required
    bool is_star{false};      // true for table.* references

    ColumnRefExpression() = default;
    ColumnRefExpression(std::string col, std::string tbl = "")
        : table_name(std::move(tbl)), column_name(std::move(col)) {}

    ASTNodeType GetType() const override { return ASTNodeType::EXPRESSION_COLUMN_REF; }
    std::string ToString() const override;
};

/** Constant / literal value */
class ConstantExpression : public ASTExpression {
public:
    Value value;

    ConstantExpression() = default;
    explicit ConstantExpression(Value val) : value(std::move(val)) {}

    ASTNodeType GetType() const override { return ASTNodeType::EXPRESSION_CONSTANT; }
    std::string ToString() const override;
};

/** Binary operation: left op right */
class BinaryOpExpression : public ASTExpression {
public:
    std::string op;  // e.g., "+", "-", "*", "/", "%", "||", "AND", "OR"
    std::unique_ptr<ASTExpression> left;
    std::unique_ptr<ASTExpression> right;

    BinaryOpExpression() = default;
    BinaryOpExpression(std::string op_name, std::unique_ptr<ASTExpression> l,
                       std::unique_ptr<ASTExpression> r)
        : op(std::move(op_name)), left(std::move(l)), right(std::move(r)) {}

    ASTNodeType GetType() const override { return ASTNodeType::EXPRESSION_BINARY_OP; }
    std::string ToString() const override;
};

/** Unary operation: NOT expr, -expr, etc. */
class UnaryOpExpression : public ASTExpression {
public:
    std::string op;  // e.g., "NOT", "-", "IS_NULL", "IS_NOT_NULL"
    std::unique_ptr<ASTExpression> operand;

    UnaryOpExpression() = default;
    UnaryOpExpression(std::string op_name, std::unique_ptr<ASTExpression> opnd)
        : op(std::move(op_name)), operand(std::move(opnd)) {}

    ASTNodeType GetType() const override { return ASTNodeType::EXPRESSION_UNARY_OP; }
    std::string ToString() const override;
};

/** Function call: func(args...) */
class FunctionCallExpression : public ASTExpression {
public:
    std::string function_name;
    std::vector<std::unique_ptr<ASTExpression>> arguments;
    bool is_distinct{false};  // COUNT(DISTINCT ...)
    bool is_star{false};      // COUNT(*)

    FunctionCallExpression() = default;

    ASTNodeType GetType() const override { return ASTNodeType::EXPRESSION_FUNCTION_CALL; }
    std::string ToString() const override;
};

/** Comparison expression: left op right (e.g., "=", "<", ">", "<=", ">=", "<>") */
class ComparisonExpression : public ASTExpression {
public:
    std::string op;
    std::unique_ptr<ASTExpression> left;
    std::unique_ptr<ASTExpression> right;

    ComparisonExpression() = default;
    ComparisonExpression(std::string op_name, std::unique_ptr<ASTExpression> l,
                         std::unique_ptr<ASTExpression> r)
        : op(std::move(op_name)), left(std::move(l)), right(std::move(r)) {}

    ASTNodeType GetType() const override { return ASTNodeType::EXPRESSION_COMPARISON; }
    std::string ToString() const override;
};

/** Star expression: SELECT * */
class StarExpression : public ASTExpression {
public:
    ASTNodeType GetType() const override { return ASTNodeType::EXPRESSION_STAR; }
    std::string ToString() const override { return "*"; }
};

/** Type cast: CAST(expr AS type) or expr::type */
class TypeCastExpression : public ASTExpression {
public:
    std::unique_ptr<ASTExpression> argument;
    DataType target_type;

    TypeCastExpression() = default;

    ASTNodeType GetType() const override { return ASTNodeType::EXPRESSION_TYPE_CAST; }
    std::string ToString() const override;
};

/** Subquery expression: (SELECT ...) in expressions */
class SubqueryExpression : public ASTExpression {
public:
    // Forward-declared; stores a SelectStatement
    std::unique_ptr<ASTNode> subquery;

    SubqueryExpression() = default;

    ASTNodeType GetType() const override { return ASTNodeType::EXPRESSION_SUBQUERY; }
    std::string ToString() const override;
};

/** CASE WHEN ... THEN ... ELSE ... END expression */
class CaseExpression : public ASTExpression {
public:
    std::unique_ptr<ASTExpression> case_operand;  // optional: CASE expr WHEN ...
    struct WhenClause {
        std::unique_ptr<ASTExpression> condition;
        std::unique_ptr<ASTExpression> result;
    };
    std::vector<WhenClause> when_clauses;
    std::unique_ptr<ASTExpression> else_result;

    CaseExpression() = default;

    ASTNodeType GetType() const override { return ASTNodeType::EXPRESSION_CASE; }
    std::string ToString() const override;
};

/** IN (list) expression: expr IN (val1, val2, ...) or expr IN (subquery) */
class InListExpression : public ASTExpression {
public:
    std::unique_ptr<ASTExpression> left;
    std::vector<std::unique_ptr<ASTExpression>> in_list;
    bool is_not{false};  // NOT IN

    InListExpression() = default;

    ASTNodeType GetType() const override { return ASTNodeType::EXPRESSION_IN_LIST; }
    std::string ToString() const override;
};

/** IS NULL / IS NOT NULL expression */
class IsNullExpression : public ASTExpression {
public:
    std::unique_ptr<ASTExpression> operand;
    bool is_not{false};  // IS NOT NULL

    IsNullExpression() = default;

    ASTNodeType GetType() const override { return ASTNodeType::EXPRESSION_IS_NULL; }
    std::string ToString() const override;
};

// =============================================================================
// Statement Nodes
// =============================================================================

/** Base class for all SQL statements */
class ASTStatement : public ASTNode {
public:
    ~ASTStatement() override = default;
};

/** SELECT statement */
class SelectStatement : public ASTStatement {
public:
    // SELECT clause
    std::vector<std::unique_ptr<ASTExpression>> select_list;
    bool is_distinct{false};

    // FROM clause
    std::string table_name;
    std::string table_alias;
    std::vector<JoinClause> joins;

    // WHERE clause
    std::unique_ptr<ASTExpression> where_clause;

    // GROUP BY clause
    std::vector<std::unique_ptr<ASTExpression>> group_by;

    // HAVING clause
    std::unique_ptr<ASTExpression> having_clause;

    // ORDER BY clause
    std::vector<OrderByClause> order_by;

    // LIMIT / OFFSET
    std::unique_ptr<ASTExpression> limit_count;
    std::unique_ptr<ASTExpression> limit_offset;

    // Subquery / UNION support (for later)
    bool is_subquery{false};

    ASTNodeType GetType() const override { return ASTNodeType::STATEMENT_SELECT; }
    std::string ToString() const override;
};

/** INSERT statement */
class InsertStatement : public ASTStatement {
public:
    std::string table_name;

    // Optional: explicit column list
    std::vector<std::string> column_names;

    // VALUES (...), (...), ... — each inner vector is one row
    std::vector<std::vector<std::unique_ptr<ASTExpression>>> value_rows;

    // Alternatively, INSERT ... SELECT
    std::unique_ptr<SelectStatement> select_source;

    ASTNodeType GetType() const override { return ASTNodeType::STATEMENT_INSERT; }
    std::string ToString() const override;
};

/** UPDATE statement */
class UpdateStatement : public ASTStatement {
public:
    std::string table_name;

    // SET clause: column_name -> value expression
    struct SetClause {
        std::string column_name;
        std::unique_ptr<ASTExpression> value;
    };
    std::vector<SetClause> set_clauses;

    // WHERE clause
    std::unique_ptr<ASTExpression> where_clause;

    ASTNodeType GetType() const override { return ASTNodeType::STATEMENT_UPDATE; }
    std::string ToString() const override;
};

/** DELETE statement */
class DeleteStatement : public ASTStatement {
public:
    std::string table_name;

    // WHERE clause
    std::unique_ptr<ASTExpression> where_clause;

    ASTNodeType GetType() const override { return ASTNodeType::STATEMENT_DELETE; }
    std::string ToString() const override;
};

/** CREATE TABLE statement */
class CreateStatement : public ASTStatement {
public:
    std::string table_name;
    std::vector<ColumnDefinition> columns;
    bool if_not_exists{false};

    ASTNodeType GetType() const override { return ASTNodeType::STATEMENT_CREATE; }
    std::string ToString() const override;
};

/** DROP TABLE statement */
class DropStatement : public ASTStatement {
public:
    std::string table_name;
    bool if_exists{false};

    ASTNodeType GetType() const override { return ASTNodeType::STATEMENT_DROP; }
    std::string ToString() const override;
};

/** CREATE INDEX statement */
class CreateIndexStatement : public ASTStatement {
public:
    std::string index_name;
    std::string table_name;
    std::string column_name;
    std::string index_type;  // "btree" or "hash"
    bool is_unique{false};
    bool if_not_exists{false};

    ASTNodeType GetType() const override { return ASTNodeType::STATEMENT_CREATE_INDEX; }
    std::string ToString() const override;
};

}  // namespace goods_db
