#include "sql/parser/parser.h"

#include <cctype>
#include <stdexcept>
#include <string>

// libpg_query headers
#include "nodes/nodes.hpp"
#include "nodes/parsenodes.hpp"
#include "nodes/pg_list.hpp"
#include "nodes/primnodes.hpp"
#include "nodes/value.hpp"
#include "postgres_parser.hpp"

namespace goods_db {

// Import all libpg_query types into scope (they live in duckdb_libpgquery)
using namespace duckdb_libpgquery;

// =============================================================================
// DataType Methods
// =============================================================================
TypeId DataType::ToTypeId() const {
    switch (kind) {
        case Kind::BOOLEAN:   return TypeId::BOOLEAN;
        case Kind::TINYINT:   return TypeId::TINYINT;
        case Kind::SMALLINT:  return TypeId::SMALLINT;
        case Kind::INTEGER:   return TypeId::INTEGER;
        case Kind::BIGINT:    return TypeId::BIGINT;
        case Kind::DECIMAL:
        case Kind::REAL:      return TypeId::DECIMAL;
        case Kind::VARCHAR:
        case Kind::TEXT:      return TypeId::VARCHAR;
        case Kind::TIMESTAMP:
        case Kind::DATE:      return TypeId::TIMESTAMP;
        default:              return TypeId::INVALID;
    }
}

std::string DataType::ToString() const {
    switch (kind) {
        case Kind::BOOLEAN:   return "BOOLEAN";
        case Kind::TINYINT:   return "TINYINT";
        case Kind::SMALLINT:  return "SMALLINT";
        case Kind::INTEGER:   return "INTEGER";
        case Kind::BIGINT:    return "BIGINT";
        case Kind::DECIMAL:   return "DECIMAL";
        case Kind::REAL:      return "REAL";
        case Kind::VARCHAR:   return length > 0 ? "VARCHAR(" + std::to_string(length) + ")" : "VARCHAR";
        case Kind::TEXT:      return "TEXT";
        case Kind::TIMESTAMP: return "TIMESTAMP";
        case Kind::DATE:      return "DATE";
        default:              return "INVALID";
    }
}

DataType DataType::FromString(const std::string& name, uint32_t length) {
    if (name == "BOOLEAN" || name == "BOOL")           return DataType(Kind::BOOLEAN);
    if (name == "TINYINT" || name == "INT1")            return DataType(Kind::TINYINT);
    if (name == "SMALLINT" || name == "INT2")           return DataType(Kind::SMALLINT);
    if (name == "INTEGER" || name == "INT" || name == "INT4") return DataType(Kind::INTEGER);
    if (name == "BIGINT" || name == "INT8")             return DataType(Kind::BIGINT);
    if (name == "DECIMAL" || name == "NUMERIC" || name == "DEC") return DataType(Kind::DECIMAL);
    if (name == "REAL" || name == "FLOAT" || name == "FLOAT4" ||
        name == "DOUBLE" || name == "FLOAT8" || name == "DOUBLE PRECISION")
        return DataType(Kind::REAL);
    if (name == "VARCHAR" || name == "CHARACTER VARYING" || name == "NVARCHAR")
        return DataType(Kind::VARCHAR, length);
    if (name == "CHAR" || name == "CHARACTER" || name == "BPCHAR")
        return DataType(Kind::VARCHAR, length > 0 ? length : 1);
    if (name == "TEXT" || name == "CLOB")               return DataType(Kind::TEXT);
    if (name == "TIMESTAMP" || name == "TIMESTAMP WITHOUT TIME ZONE")
        return DataType(Kind::TIMESTAMP);
    if (name == "DATE")                                 return DataType(Kind::DATE);
    return DataType(Kind::INVALID);
}

// =============================================================================
// ColumnDefinition::ToString
// =============================================================================
std::string ColumnDefinition::ToString() const {
    std::string result = column_name + " " + data_type.ToString();
    if (!is_nullable) result += " NOT NULL";
    if (is_primary_key) result += " PRIMARY KEY";
    if (is_unique) result += " UNIQUE";
    if (!default_value.empty()) result += " DEFAULT " + default_value;
    return result;
}

// =============================================================================
// OrderByClause::ToString
// =============================================================================
std::string OrderByClause::ToString() const {
    std::string result = expression ? expression->ToString() : "?";
    if (order_type == OrderType::ASC) result += " ASC";
    else if (order_type == OrderType::DESC) result += " DESC";
    return result;
}

// =============================================================================
// JoinClause::ToString
// =============================================================================
std::string JoinClause::ToString() const {
    std::string result;
    switch (join_type) {
        case JoinType::INNER:   result = "INNER JOIN"; break;
        case JoinType::LEFT:    result = "LEFT JOIN"; break;
        case JoinType::RIGHT:   result = "RIGHT JOIN"; break;
        case JoinType::FULL:    result = "FULL JOIN"; break;
        case JoinType::CROSS:   result = "CROSS JOIN"; break;
        case JoinType::NATURAL: result = "NATURAL JOIN"; break;
    }
    result += " " + table_name;
    if (!alias.empty()) result += " AS " + alias;
    if (condition) result += " ON " + condition->ToString();
    return result;
}

// =============================================================================
// Expression Node ToString Implementations
// =============================================================================
std::string ColumnRefExpression::ToString() const {
    if (is_star) return table_name.empty() ? "*" : table_name + ".*";
    if (table_name.empty()) return column_name;
    return table_name + "." + column_name;
}

std::string ConstantExpression::ToString() const {
    return value.ToString();
}

std::string BinaryOpExpression::ToString() const {
    std::string result = "(" + (left ? left->ToString() : "?");
    result += " " + op + " ";
    result += (right ? right->ToString() : "?") + ")";
    return result;
}

std::string UnaryOpExpression::ToString() const {
    return op + "(" + (operand ? operand->ToString() : "?") + ")";
}

std::string FunctionCallExpression::ToString() const {
    std::string result = function_name + "(";
    if (is_star) {
        result += "*";
    } else {
        if (is_distinct) result += "DISTINCT ";
        for (size_t i = 0; i < arguments.size(); i++) {
            if (i > 0) result += ", ";
            result += arguments[i] ? arguments[i]->ToString() : "?";
        }
    }
    result += ")";
    return result;
}

std::string ComparisonExpression::ToString() const {
    return (left ? left->ToString() : "?") + " " + op + " " +
           (right ? right->ToString() : "?");
}

std::string TypeCastExpression::ToString() const {
    return "CAST(" + (argument ? argument->ToString() : "?") +
           " AS " + target_type.ToString() + ")";
}

std::string SubqueryExpression::ToString() const {
    return "(subquery)";
}

std::string CaseExpression::ToString() const {
    std::string result = "CASE";
    if (case_operand) result += " " + case_operand->ToString();
    for (const auto& wc : when_clauses) {
        result += " WHEN " + (wc.condition ? wc.condition->ToString() : "?");
        result += " THEN " + (wc.result ? wc.result->ToString() : "?");
    }
    if (else_result) result += " ELSE " + else_result->ToString();
    result += " END";
    return result;
}

std::string InListExpression::ToString() const {
    std::string result = (left ? left->ToString() : "?");
    result += is_not ? " NOT IN (" : " IN (";
    for (size_t i = 0; i < in_list.size(); i++) {
        if (i > 0) result += ", ";
        result += in_list[i] ? in_list[i]->ToString() : "?";
    }
    result += ")";
    return result;
}

std::string IsNullExpression::ToString() const {
    return (operand ? operand->ToString() : "?") +
           (is_not ? " IS NOT NULL" : " IS NULL");
}

// =============================================================================
// Statement Node ToString Implementations
// =============================================================================
std::string SelectStatement::ToString() const {
    std::string result = "SELECT ";
    if (is_distinct) result += "DISTINCT ";
    for (size_t i = 0; i < select_list.size(); i++) {
        if (i > 0) result += ", ";
        result += select_list[i] ? select_list[i]->ToString() : "?";
    }
    if (!table_name.empty()) {
        result += " FROM " + table_name;
        if (!table_alias.empty()) result += " AS " + table_alias;
        for (const auto& join : joins) {
            result += " " + join.ToString();
        }
    }
    if (where_clause) result += " WHERE " + where_clause->ToString();
    if (!group_by.empty()) {
        result += " GROUP BY ";
        for (size_t i = 0; i < group_by.size(); i++) {
            if (i > 0) result += ", ";
            result += group_by[i] ? group_by[i]->ToString() : "?";
        }
    }
    if (having_clause) result += " HAVING " + having_clause->ToString();
    if (!order_by.empty()) {
        result += " ORDER BY ";
        for (size_t i = 0; i < order_by.size(); i++) {
            if (i > 0) result += ", ";
            result += order_by[i].ToString();
        }
    }
    if (limit_count) result += " LIMIT " + limit_count->ToString();
    if (limit_offset) result += " OFFSET " + limit_offset->ToString();
    return result;
}

std::string InsertStatement::ToString() const {
    std::string result = "INSERT INTO " + table_name;
    if (!column_names.empty()) {
        result += "(";
        for (size_t i = 0; i < column_names.size(); i++) {
            if (i > 0) result += ", ";
            result += column_names[i];
        }
        result += ")";
    }
    if (select_source) {
        result += " " + select_source->ToString();
    } else {
        result += " VALUES ";
        for (size_t r = 0; r < value_rows.size(); r++) {
            if (r > 0) result += ", ";
            result += "(";
            for (size_t c = 0; c < value_rows[r].size(); c++) {
                if (c > 0) result += ", ";
                result += value_rows[r][c] ? value_rows[r][c]->ToString() : "?";
            }
            result += ")";
        }
    }
    return result;
}

std::string UpdateStatement::ToString() const {
    std::string result = "UPDATE " + table_name + " SET ";
    for (size_t i = 0; i < set_clauses.size(); i++) {
        if (i > 0) result += ", ";
        result += set_clauses[i].column_name + " = " +
                  (set_clauses[i].value ? set_clauses[i].value->ToString() : "?");
    }
    if (where_clause) result += " WHERE " + where_clause->ToString();
    return result;
}

std::string DeleteStatement::ToString() const {
    std::string result = "DELETE FROM " + table_name;
    if (where_clause) result += " WHERE " + where_clause->ToString();
    return result;
}

std::string CreateStatement::ToString() const {
    std::string result = "CREATE TABLE ";
    if (if_not_exists) result += "IF NOT EXISTS ";
    result += table_name + " (";
    for (size_t i = 0; i < columns.size(); i++) {
        if (i > 0) result += ", ";
        result += columns[i].ToString();
    }
    result += ")";
    return result;
}

std::string DropStatement::ToString() const {
    std::string result = "DROP TABLE ";
    if (if_exists) result += "IF EXISTS ";
    result += table_name;
    return result;
}

std::string CreateIndexStatement::ToString() const {
    std::string result;
    if (is_unique) result += "UNIQUE ";
    result += "INDEX ";
    if (if_not_exists) result += "IF NOT EXISTS ";
    result += index_name + " ON " + table_name + " (" + column_name + ")";
    if (!index_type.empty()) result += " USING " + index_type;
    return result;
}

// =============================================================================
// Parser PIMPL and Core Implementation
// =============================================================================
struct Parser::Impl {
    duckdb::PostgresParser pg_parser;
};

Parser::Parser() : impl_(std::make_unique<Impl>()) {}
Parser::~Parser() = default;

std::vector<std::unique_ptr<ASTStatement>> Parser::Parse(const std::string& sql) {
    std::vector<std::unique_ptr<ASTStatement>> result;
    if (!TryParse(sql, result)) {
        throw std::runtime_error("Parse error: " + error_message_);
    }
    return result;
}

bool Parser::TryParse(const std::string& sql,
                      std::vector<std::unique_ptr<ASTStatement>>& result) {
    result.clear();
    error_message_.clear();

    try {
        impl_->pg_parser.Parse(sql);
    } catch (const std::exception& e) {
        error_message_ = e.what();
        return false;
    }

    if (!impl_->pg_parser.success) {
        error_message_ = impl_->pg_parser.error_message;
        return false;
    }

    auto* parse_tree = impl_->pg_parser.parse_tree;
    if (parse_tree == nullptr) {
        return true;  // empty input
    }

    // Iterate the PGList of parse tree nodes.
    // Each top-level node is a PGRawStmt wrapper containing the actual statement.
    PGListCell* cell = nullptr;
    foreach (cell, parse_tree) {
        auto* raw_node = lfirst(cell);
        if (raw_node == nullptr) continue;

        // Unwrap PGRawStmt
        if (nodeTag(raw_node) != T_PGRawStmt) continue;
        auto* raw_stmt = reinterpret_cast<PGRawStmt*>(raw_node);
        auto* pg_stmt = raw_stmt->stmt;
        if (pg_stmt == nullptr) continue;

        auto tag = nodeTag(pg_stmt);
        std::unique_ptr<ASTStatement> stmt;

        switch (tag) {
            case T_PGSelectStmt:
                stmt = TranslateSelectStmt(pg_stmt);
                break;
            case T_PGInsertStmt:
                stmt = TranslateInsertStmt(pg_stmt);
                break;
            case T_PGUpdateStmt:
                stmt = TranslateUpdateStmt(pg_stmt);
                break;
            case T_PGDeleteStmt:
                stmt = TranslateDeleteStmt(pg_stmt);
                break;
            case T_PGCreateStmt:
                stmt = TranslateCreateStmt(pg_stmt);
                break;
            case T_PGDropStmt:
                stmt = TranslateDropStmt(pg_stmt);
                break;
            case T_PGIndexStmt:
                stmt = TranslateIndexStmt(pg_stmt);
                break;
            default:
                // Unknown statement type — skip
                continue;
        }

        if (stmt) {
            result.push_back(std::move(stmt));
        }
    }

    return true;
}

// =============================================================================
// Statement Translation
// =============================================================================
std::unique_ptr<SelectStatement> Parser::TranslateSelectStmt(void* pg_node) {
    auto* stmt = castNode(PGSelectStmt, pg_node);
    auto result = std::make_unique<SelectStatement>();

    // targetList → select_list
    if (stmt->targetList != nullptr) {
        PGListCell* cell = nullptr;
        foreach (cell, stmt->targetList) {
            auto* res_target = castNode(PGResTarget, lfirst(cell));
            if (res_target->val != nullptr) {
                auto expr = TranslateExpression(res_target->val);
                if (expr) {
                    if (res_target->name != nullptr) {
                        expr->alias = std::string(res_target->name);
                    }
                    result->select_list.push_back(std::move(expr));
                }
            }
        }
    }

    // DISTINCT
    if (stmt->distinctClause != nullptr) {
        result->is_distinct = (list_length(stmt->distinctClause) > 0);
    }

    // FROM clause
    if (stmt->fromClause != nullptr && list_length(stmt->fromClause) > 0) {
        auto* first_node = linitial(stmt->fromClause);
        auto first_tag = nodeTag(first_node);

        if (first_tag == T_PGRangeVar) {
            // Simple table reference
            result->table_name = ExtractRelationName(first_node);
            auto* rv = castNode(PGRangeVar, first_node);
            if (rv->alias != nullptr && rv->alias->aliasname != nullptr) {
                result->table_alias = std::string(rv->alias->aliasname);
            }
        } else if (first_tag == T_PGJoinExpr) {
            // JOIN tree: leftmost leaf is base table, rest are joins
            ExtractBaseTableFromJoin(first_node, result.get());
            FlattenJoinTree(first_node, result.get());
        } else if (first_tag == T_PGRangeSubselect) {
            auto* rs = castNode(PGRangeSubselect, first_node);
            if (rs->alias != nullptr && rs->alias->aliasname != nullptr) {
                result->table_name = std::string(rs->alias->aliasname);
            }
        }

        // Handle FROM items after the first (comma-separated tables = implicit cross joins)
        if (list_length(stmt->fromClause) > 1) {
            PGListCell* cell = nullptr;
            bool first = true;
            foreach (cell, stmt->fromClause) {
                if (first) { first = false; continue; }
                auto* extra_node = lfirst(cell);
                auto extra_tag = nodeTag(extra_node);
                if (extra_tag == T_PGJoinExpr) {
                    // If base table not yet set, extract from this join tree
                    if (result->table_name.empty()) {
                        ExtractBaseTableFromJoin(extra_node, result.get());
                    }
                    FlattenJoinTree(extra_node, result.get());
                } else if (extra_tag == T_PGRangeVar) {
                    JoinClause jc;
                    jc.join_type = JoinType::CROSS;
                    jc.table_name = ExtractRelationName(extra_node);
                    auto* rv = castNode(PGRangeVar, extra_node);
                    if (rv->alias != nullptr && rv->alias->aliasname != nullptr) {
                        jc.alias = std::string(rv->alias->aliasname);
                    }
                    result->joins.push_back(std::move(jc));
                }
            }
        }
    }

    // WHERE clause
    if (stmt->whereClause != nullptr) {
        result->where_clause = TranslateExpression(stmt->whereClause);
    }

    // GROUP BY
    if (stmt->groupClause != nullptr) {
        PGListCell* cell = nullptr;
        foreach (cell, stmt->groupClause) {
            auto expr = TranslateExpression(lfirst(cell));
            if (expr) result->group_by.push_back(std::move(expr));
        }
    }

    // HAVING
    if (stmt->havingClause != nullptr) {
        result->having_clause = TranslateExpression(stmt->havingClause);
    }

    // ORDER BY
    if (stmt->sortClause != nullptr) {
        PGListCell* cell = nullptr;
        foreach (cell, stmt->sortClause) {
            result->order_by.push_back(TranslateSortBy(lfirst(cell)));
        }
    }

    // LIMIT / OFFSET
    if (stmt->limitCount != nullptr) {
        result->limit_count = TranslateExpression(stmt->limitCount);
    }
    if (stmt->limitOffset != nullptr) {
        result->limit_offset = TranslateExpression(stmt->limitOffset);
    }

    return result;
}

std::unique_ptr<InsertStatement> Parser::TranslateInsertStmt(void* pg_node) {
    auto* stmt = castNode(PGInsertStmt, pg_node);
    auto result = std::make_unique<InsertStatement>();

    result->table_name = ExtractRelationName(stmt->relation);

    // Column list
    if (stmt->cols != nullptr) {
        PGListCell* cell = nullptr;
        foreach (cell, stmt->cols) {
            auto* res_target = castNode(PGResTarget, lfirst(cell));
            if (res_target->name != nullptr) {
                result->column_names.push_back(std::string(res_target->name));
            }
        }
    }

    // Source: either SELECT or VALUES
    if (stmt->selectStmt != nullptr) {
        auto select_tag = nodeTag(stmt->selectStmt);
        if (select_tag == T_PGSelectStmt) {
            auto* sel = castNode(PGSelectStmt, stmt->selectStmt);
            // VALUES clause
            if (sel->valuesLists != nullptr) {
                PGListCell* row_cell = nullptr;
                foreach (row_cell, sel->valuesLists) {
                    auto* row_list = reinterpret_cast<PGList*>(lfirst(row_cell));
                    std::vector<std::unique_ptr<ASTExpression>> row;
                    PGListCell* val_cell = nullptr;
                    foreach (val_cell, row_list) {
                        auto expr = TranslateExpression(lfirst(val_cell));
                        if (expr) row.push_back(std::move(expr));
                    }
                    result->value_rows.push_back(std::move(row));
                }
            } else {
                // INSERT ... SELECT
                result->select_source = TranslateSelectStmt(stmt->selectStmt);
            }
        }
    }

    return result;
}

std::unique_ptr<UpdateStatement> Parser::TranslateUpdateStmt(void* pg_node) {
    auto* stmt = castNode(PGUpdateStmt, pg_node);
    auto result = std::make_unique<UpdateStatement>();

    result->table_name = ExtractRelationName(stmt->relation);

    // SET clause (targetList)
    if (stmt->targetList != nullptr) {
        PGListCell* cell = nullptr;
        foreach (cell, stmt->targetList) {
            auto* res_target = castNode(PGResTarget, lfirst(cell));
            UpdateStatement::SetClause sc;
            if (res_target->name != nullptr) {
                sc.column_name = std::string(res_target->name);
            }
            if (res_target->val != nullptr) {
                sc.value = TranslateExpression(res_target->val);
            }
            result->set_clauses.push_back(std::move(sc));
        }
    }

    // WHERE clause
    if (stmt->whereClause != nullptr) {
        result->where_clause = TranslateExpression(stmt->whereClause);
    }

    return result;
}

std::unique_ptr<DeleteStatement> Parser::TranslateDeleteStmt(void* pg_node) {
    auto* stmt = castNode(PGDeleteStmt, pg_node);
    auto result = std::make_unique<DeleteStatement>();

    result->table_name = ExtractRelationName(stmt->relation);

    if (stmt->whereClause != nullptr) {
        result->where_clause = TranslateExpression(stmt->whereClause);
    }

    return result;
}

std::unique_ptr<CreateStatement> Parser::TranslateCreateStmt(void* pg_node) {
    auto* stmt = castNode(PGCreateStmt, pg_node);
    auto result = std::make_unique<CreateStatement>();

    result->table_name = ExtractRelationName(stmt->relation);

    // IF NOT EXISTS
    result->if_not_exists = (stmt->onconflict == PG_IGNORE_ON_CONFLICT);

    // Column definitions
    if (stmt->tableElts != nullptr) {
        PGListCell* cell = nullptr;
        foreach (cell, stmt->tableElts) {
            auto* node = lfirst(cell);
            if (nodeTag(node) == T_PGColumnDef) {
                result->columns.push_back(TranslateColumnDef(node));
            }
            // Skip constraints for now (T_PGConstraint)
        }
    }

    return result;
}

std::unique_ptr<DropStatement> Parser::TranslateDropStmt(void* pg_node) {
    auto* stmt = castNode(PGDropStmt, pg_node);
    auto result = std::make_unique<DropStatement>();

    // Extract table name from objects list
    if (stmt->objects != nullptr && list_length(stmt->objects) > 0) {
        auto* name_list = reinterpret_cast<PGList*>(linitial(stmt->objects));
        if (name_list != nullptr && list_length(name_list) > 0) {
            auto* val = castNode(PGValue, linitial(name_list));
            result->table_name = std::string(strVal(val));
        }
    }

    result->if_exists = stmt->missing_ok;

    return result;
}

std::unique_ptr<CreateIndexStatement> Parser::TranslateIndexStmt(void* pg_node) {
    auto* stmt = castNode(PGIndexStmt, pg_node);
    auto result = std::make_unique<CreateIndexStatement>();

    // Index name
    if (stmt->idxname != nullptr) {
        result->index_name = std::string(stmt->idxname);
    }

    // Table name
    result->table_name = ExtractRelationName(stmt->relation);

    // Column name (take first index param)
    if (stmt->indexParams != nullptr && list_length(stmt->indexParams) > 0) {
        auto* ielem = reinterpret_cast<PGIndexElem*>(linitial(stmt->indexParams));
        if (ielem->name != nullptr) {
            result->column_name = std::string(ielem->name);
        }
    }

    // Access method
    if (stmt->accessMethod != nullptr) {
        result->index_type = std::string(stmt->accessMethod);
    }

    result->is_unique = stmt->unique;

    return result;
}

// =============================================================================
// Expression Translation
// =============================================================================
std::unique_ptr<ASTExpression> Parser::TranslateExpression(void* pg_node) {
    if (pg_node == nullptr) return nullptr;

    auto tag = nodeTag(pg_node);

    switch (tag) {
        case T_PGColumnRef:    return TranslateColumnRef(pg_node);
        case T_PGAConst:       return TranslateConstant(pg_node);
        case T_PGAExpr:        return TranslateAExpr(pg_node);
        case T_PGBoolExpr:     return TranslateBoolExpr(pg_node);
        case T_PGFuncCall:     return TranslateFuncCall(pg_node);
        case T_PGTypeCast:     return TranslateTypeCast(pg_node);
        case T_PGSubLink:      return TranslateSubLink(pg_node);
        case T_PGCaseExpr:     return TranslateCaseExpr(pg_node);
        case T_PGNullTest:     return TranslateNullTest(pg_node);
        case T_PGAStar:        return TranslateStarExpr(pg_node);
        case T_PGCoalesceExpr: {
            // Translate COALESCE as a function call
            auto* ce = castNode(PGCoalesceExpr, pg_node);
            auto func = std::make_unique<FunctionCallExpression>();
            func->function_name = "COALESCE";
            if (ce->args != nullptr) {
                PGListCell* cell = nullptr;
                foreach (cell, ce->args) {
                    auto arg = TranslateExpression(lfirst(cell));
                    if (arg) func->arguments.push_back(std::move(arg));
                }
            }
            return func;
        }
        case T_PGInteger:
        case T_PGFloat:
        case T_PGString:
        case T_PGNull:
            return TranslateConstant(pg_node);
        case T_PGList: {
            // Parenthesized expression list — return the first element
            auto* lst = reinterpret_cast<PGList*>(pg_node);
            if (lst->length > 0) {
                return TranslateExpression(linitial(lst));
            }
            return nullptr;
        }
        case T_PGMultiAssignRef: {
            // Row source for multi-column assignment — unwrap
            auto* mar = castNode(PGMultiAssignRef, pg_node);
            return TranslateExpression(mar->source);
        }
        case T_PGParamRef:
            // Parameter reference ($1, $2) — treat as placeholder
            return nullptr;
        default:
            // Unknown expression type — return nullptr
            return nullptr;
    }
}

std::unique_ptr<ASTExpression> Parser::TranslateColumnRef(void* pg_node) {
    auto* col_ref = castNode(PGColumnRef, pg_node);
    auto result = std::make_unique<ColumnRefExpression>();

    if (col_ref->fields != nullptr) {
        int num_fields = list_length(col_ref->fields);

        if (num_fields >= 1) {
            auto* first = linitial(col_ref->fields);
            if (nodeTag(first) == T_PGAStar) {
                result->is_star = true;
                auto* star = castNode(PGAStar, first);
                if (star->relation != nullptr) {
                    result->table_name = std::string(star->relation);
                }
                return result;
            }
            result->column_name = GetStringFromValue(first);
        }

        if (num_fields >= 2) {
            // Qualified name: table.column
            auto* second = lsecond(col_ref->fields);
            result->table_name = result->column_name;
            result->column_name = GetStringFromValue(second);
        }
    }

    return result;
}

std::unique_ptr<ASTExpression> Parser::TranslateConstant(void* pg_node) {
    auto result = std::make_unique<ConstantExpression>();
    result->value = TranslateValue(pg_node);
    return result;
}

std::unique_ptr<ASTExpression> Parser::TranslateAExpr(void* pg_node) {
    auto* a_expr = castNode(PGAExpr, pg_node);
    auto* name_list = a_expr->name;

    // Determine the operator string
    std::string op_name;
    if (name_list != nullptr && list_length(name_list) > 0) {
        op_name = GetStringFromValue(linitial(name_list));
    }

    auto kind = a_expr->kind;

    // Handle specific AExpr kinds
    switch (kind) {
        case PG_AEXPR_OP: {
            // Normal binary operator
            auto left = TranslateExpression(a_expr->lexpr);
            auto right = TranslateExpression(a_expr->rexpr);

            if (op_name == "=" || op_name == "<" || op_name == ">" ||
                op_name == "<=" || op_name == ">=" || op_name == "<>" || op_name == "!=") {
                // Comparison expression
                return std::make_unique<ComparisonExpression>(
                    std::move(op_name), std::move(left), std::move(right));
            }

            // Logical operators
            if (op_name == "AND" || op_name == "OR") {
                return std::make_unique<BinaryOpExpression>(
                    std::move(op_name), std::move(left), std::move(right));
            }

            // General binary operation
            auto binop = std::make_unique<BinaryOpExpression>(
                std::move(op_name), std::move(left), std::move(right));
            if (a_expr->lexpr == nullptr) {
                // Unary prefix operator
                return std::make_unique<UnaryOpExpression>(
                    binop->op, std::move(binop->right));
            }
            return binop;
        }

        case PG_AEXPR_IN: {
            auto in_expr = std::make_unique<InListExpression>();
            in_expr->left = TranslateExpression(a_expr->lexpr);
            // Right side is a list
            if (a_expr->rexpr != nullptr) {
                auto rexpr_tag = nodeTag(a_expr->rexpr);
                if (rexpr_tag == T_PGList) {
                    auto* lst = reinterpret_cast<PGList*>(a_expr->rexpr);
                    PGListCell* cell = nullptr;
                    foreach (cell, lst) {
                        auto val = TranslateExpression(lfirst(cell));
                        if (val) in_expr->in_list.push_back(std::move(val));
                    }
                } else {
                    // Subquery or single expression
                    auto val = TranslateExpression(a_expr->rexpr);
                    if (val) in_expr->in_list.push_back(std::move(val));
                }
            }
            return in_expr;
        }

        case PG_AEXPR_LIKE:
        case PG_AEXPR_ILIKE:
        case PG_AEXPR_GLOB:
        case PG_AEXPR_SIMILAR: {
            auto left = TranslateExpression(a_expr->lexpr);
            auto right = TranslateExpression(a_expr->rexpr);
            return std::make_unique<ComparisonExpression>(
                std::move(op_name), std::move(left), std::move(right));
        }

        case PG_AEXPR_BETWEEN:
        case PG_AEXPR_NOT_BETWEEN:
        case PG_AEXPR_BETWEEN_SYM:
        case PG_AEXPR_NOT_BETWEEN_SYM: {
            // BETWEEN is represented as "a BETWEEN x AND y"
            // We store it as a function-like expression
            auto func = std::make_unique<FunctionCallExpression>();
            func->function_name = (kind == PG_AEXPR_NOT_BETWEEN || kind == PG_AEXPR_NOT_BETWEEN_SYM)
                                      ? "NOT_BETWEEN" : "BETWEEN";
            auto left = TranslateExpression(a_expr->lexpr);
            if (left) func->arguments.push_back(std::move(left));
            if (a_expr->rexpr != nullptr) {
                auto rexpr_tag = nodeTag(a_expr->rexpr);
                if (rexpr_tag == T_PGList) {
                    auto* lst = reinterpret_cast<PGList*>(a_expr->rexpr);
                    PGListCell* cell = nullptr;
                    foreach (cell, lst) {
                        auto val = TranslateExpression(lfirst(cell));
                        if (val) func->arguments.push_back(std::move(val));
                    }
                }
            }
            return func;
        }

        case PG_AEXPR_DISTINCT: {
            auto left = TranslateExpression(a_expr->lexpr);
            auto right = TranslateExpression(a_expr->rexpr);
            return std::make_unique<ComparisonExpression>(
                "IS DISTINCT FROM", std::move(left), std::move(right));
        }

        case PG_AEXPR_NOT_DISTINCT: {
            auto left = TranslateExpression(a_expr->lexpr);
            auto right = TranslateExpression(a_expr->rexpr);
            return std::make_unique<ComparisonExpression>(
                "IS NOT DISTINCT FROM", std::move(left), std::move(right));
        }

        default:
            // Fallback: treat as binary op
            {
                auto left = TranslateExpression(a_expr->lexpr);
                auto right = TranslateExpression(a_expr->rexpr);
                return std::make_unique<BinaryOpExpression>(
                    std::move(op_name), std::move(left), std::move(right));
            }
    }
}

std::unique_ptr<ASTExpression> Parser::TranslateBoolExpr(void* pg_node) {
    auto* bool_expr = castNode(PGBoolExpr, pg_node);
    std::string op;

    switch (bool_expr->boolop) {
        case PG_AND_EXPR: op = "AND"; break;
        case PG_OR_EXPR:  op = "OR"; break;
        case PG_NOT_EXPR: op = "NOT"; break;
        default:          op = "AND"; break;
    }

    if (bool_expr->boolop == PG_NOT_EXPR) {
        // Unary NOT
        auto operand = (bool_expr->args != nullptr && list_length(bool_expr->args) > 0)
                           ? TranslateExpression(linitial(bool_expr->args))
                           : nullptr;
        return std::make_unique<UnaryOpExpression>(std::move(op), std::move(operand));
    }

    // AND/OR: chain binary expressions left-deep
    if (bool_expr->args == nullptr || list_length(bool_expr->args) == 0) {
        return nullptr;
    }

    std::unique_ptr<ASTExpression> result;
    PGListCell* cell = nullptr;
    foreach (cell, bool_expr->args) {
        auto arg = TranslateExpression(lfirst(cell));
        if (!arg) continue;

        if (!result) {
            result = std::move(arg);
        } else {
            result = std::make_unique<BinaryOpExpression>(
                op, std::move(result), std::move(arg));
        }
    }

    return result;
}

std::unique_ptr<ASTExpression> Parser::TranslateFuncCall(void* pg_node) {
    auto* func_call = castNode(PGFuncCall, pg_node);
    auto result = std::make_unique<FunctionCallExpression>();

    // Function name (normalize to uppercase for case-insensitivity)
    if (func_call->funcname != nullptr && list_length(func_call->funcname) > 0) {
        result->function_name = GetStringFromValue(linitial(func_call->funcname));
        // Convert to uppercase for case-insensitive matching
        for (auto& c : result->function_name) {
            c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
        }
    }

    result->is_distinct = func_call->agg_distinct;
    result->is_star = func_call->agg_star;

    // Arguments
    if (!func_call->agg_star && func_call->args != nullptr) {
        PGListCell* cell = nullptr;
        foreach (cell, func_call->args) {
            auto arg = TranslateExpression(lfirst(cell));
            if (arg) result->arguments.push_back(std::move(arg));
        }
    }

    return result;
}

std::unique_ptr<ASTExpression> Parser::TranslateTypeCast(void* pg_node) {
    auto* tc = castNode(PGTypeCast, pg_node);
    auto result = std::make_unique<TypeCastExpression>();

    if (tc->arg != nullptr) {
        result->argument = TranslateExpression(tc->arg);
    }
    if (tc->typeName != nullptr) {
        result->target_type = TranslateTypeName(tc->typeName);
    }

    return result;
}

std::unique_ptr<ASTExpression> Parser::TranslateSubLink(void* pg_node) {
    auto* sl = castNode(PGSubLink, pg_node);
    auto result = std::make_unique<SubqueryExpression>();

    if (sl->subselect != nullptr) {
        auto tag = nodeTag(sl->subselect);
        if (tag == T_PGSelectStmt) {
            result->subquery = TranslateSelectStmt(sl->subselect);
        }
    }

    return result;
}

std::unique_ptr<ASTExpression> Parser::TranslateCaseExpr(void* pg_node) {
    auto* ce = castNode(PGCaseExpr, pg_node);
    auto result = std::make_unique<CaseExpression>();

    // CASE operand (optional)
    if (ce->arg != nullptr) {
        result->case_operand = TranslateExpression(ce->arg);
    }

    // WHEN clauses
    if (ce->args != nullptr) {
        PGListCell* cell = nullptr;
        foreach (cell, ce->args) {
            auto* wc = castNode(PGCaseWhen, lfirst(cell));
            CaseExpression::WhenClause when;
            if (wc->expr != nullptr) {
                when.condition = TranslateExpression(wc->expr);
            }
            if (wc->result != nullptr) {
                when.result = TranslateExpression(wc->result);
            }
            result->when_clauses.push_back(std::move(when));
        }
    }

    // ELSE result
    if (ce->defresult != nullptr) {
        result->else_result = TranslateExpression(ce->defresult);
    }

    return result;
}

std::unique_ptr<ASTExpression> Parser::TranslateNullTest(void* pg_node) {
    auto* nt = castNode(PGNullTest, pg_node);
    auto result = std::make_unique<IsNullExpression>();

    if (nt->arg != nullptr) {
        result->operand = TranslateExpression(nt->arg);
    }
    result->is_not = (nt->nulltesttype == IS_NOT_NULL);

    return result;
}

std::unique_ptr<ASTExpression> Parser::TranslateStarExpr(void* /*pg_node*/) {
    auto result = std::make_unique<StarExpression>();
    // PGAStar may have relation info — we keep it simple, just return *
    return result;
}

// =============================================================================
// Utility Helpers
// =============================================================================
DataType Parser::TranslateTypeName(void* pg_type_name) {
    if (pg_type_name == nullptr) return DataType();

    auto* tn = castNode(PGTypeName, pg_type_name);
    std::string type_name;

    if (tn->names != nullptr && list_length(tn->names) > 0) {
        type_name = GetStringFromValue(linitial(tn->names));
    }

    // Get type modifier (e.g., length for VARCHAR(n))
    uint32_t length = 0;
    if (tn->typmods != nullptr && list_length(tn->typmods) > 0) {
        auto* mod = linitial(tn->typmods);
        if (nodeTag(mod) == T_PGAConst) {
            auto* aconst = castNode(PGAConst, mod);
            length = static_cast<uint32_t>(intVal(&aconst->val));
        }
    }

    return DataType::FromString(type_name, length);
}

ColumnDefinition Parser::TranslateColumnDef(void* pg_col_def) {
    auto* cd = castNode(PGColumnDef, pg_col_def);
    ColumnDefinition col_def;

    if (cd->colname != nullptr) {
        col_def.column_name = std::string(cd->colname);
    }

    if (cd->typeName != nullptr) {
        col_def.data_type = TranslateTypeName(cd->typeName);
    }

    col_def.is_nullable = !cd->is_not_null;

    // Check constraints for PRIMARY KEY / UNIQUE
    if (cd->constraints != nullptr) {
        PGListCell* cell = nullptr;
        foreach (cell, cd->constraints) {
            auto* constraint = castNode(PGConstraint, lfirst(cell));
            switch (constraint->contype) {
                case PG_CONSTR_PRIMARY:
                    col_def.is_primary_key = true;
                    col_def.is_nullable = false;
                    break;
                case PG_CONSTR_UNIQUE:
                    col_def.is_unique = true;
                    break;
                case PG_CONSTR_NOTNULL:
                    col_def.is_nullable = false;
                    break;
                case PG_CONSTR_DEFAULT:
                    // Default value — simplified (just note it exists)
                    col_def.default_value = "<default>";
                    break;
                default:
                    break;
            }
        }
    }

    return col_def;
}

OrderByClause Parser::TranslateSortBy(void* pg_sort_by) {
    auto* sb = castNode(PGSortBy, pg_sort_by);
    OrderByClause result;

    if (sb->node != nullptr) {
        result.expression = TranslateExpression(sb->node);
    }

    switch (sb->sortby_dir) {
        case PG_SORTBY_ASC:  result.order_type = OrderType::ASC; break;
        case PG_SORTBY_DESC: result.order_type = OrderType::DESC; break;
        default:             result.order_type = OrderType::DEFAULT; break;
    }

    return result;
}

void Parser::ExtractBaseTableFromJoin(void* pg_join, SelectStatement* result) {
    auto* je = castNode(PGJoinExpr, pg_join);
    // Walk down the left side until we find a PGRangeVar
    void* current = je->larg;
    while (current != nullptr) {
        auto tag = nodeTag(current);
        if (tag == T_PGRangeVar) {
            result->table_name = ExtractRelationName(current);
            auto* rv = castNode(PGRangeVar, current);
            if (rv->alias != nullptr && rv->alias->aliasname != nullptr) {
                result->table_alias = std::string(rv->alias->aliasname);
            }
            return;
        } else if (tag == T_PGJoinExpr) {
            current = castNode(PGJoinExpr, current)->larg;
        } else {
            break;
        }
    }
}

void Parser::FlattenJoinTree(void* pg_node, SelectStatement* result) {
    auto tag = nodeTag(pg_node);
    if (tag != T_PGJoinExpr) return;

    auto* je = castNode(PGJoinExpr, pg_node);

    // Recursively flatten the left side first
    if (je->larg != nullptr) {
        auto larg_tag = nodeTag(je->larg);
        if (larg_tag == T_PGJoinExpr) {
            FlattenJoinTree(je->larg, result);
        }
    }

    // Add the current join level (the right-hand side)
    JoinClause jc;
    switch (je->jointype) {
        case PG_JOIN_INNER: jc.join_type = JoinType::INNER; break;
        case PG_JOIN_LEFT:  jc.join_type = JoinType::LEFT; break;
        case PG_JOIN_RIGHT: jc.join_type = JoinType::RIGHT; break;
        case PG_JOIN_FULL:  jc.join_type = JoinType::FULL; break;
        default:            jc.join_type = JoinType::INNER; break;
    }
    if (je->isNatural) jc.join_type = JoinType::NATURAL;

    // Right side table
    if (je->rarg != nullptr) {
        jc.table_name = ExtractRelationName(je->rarg);
        auto* rv = castNode(PGRangeVar, je->rarg);
        if (rv->alias != nullptr && rv->alias->aliasname != nullptr) {
            jc.alias = std::string(rv->alias->aliasname);
        }
    }

    // ON condition
    if (je->quals != nullptr) {
        jc.condition = TranslateExpression(je->quals);
    }

    // USING clause
    if (je->usingClause != nullptr) {
        PGListCell* cell = nullptr;
        foreach (cell, je->usingClause) {
            jc.using_columns.push_back(GetStringFromValue(lfirst(cell)));
        }
    }

    result->joins.push_back(std::move(jc));
}

JoinClause Parser::TranslateJoinExpr(void* pg_join) {
    auto* je = castNode(PGJoinExpr, pg_join);
    JoinClause result;

    // Join type
    switch (je->jointype) {
        case PG_JOIN_INNER: result.join_type = JoinType::INNER; break;
        case PG_JOIN_LEFT:  result.join_type = JoinType::LEFT; break;
        case PG_JOIN_RIGHT: result.join_type = JoinType::RIGHT; break;
        case PG_JOIN_FULL:  result.join_type = JoinType::FULL; break;
        default:            result.join_type = JoinType::INNER; break;
    }

    if (je->isNatural) {
        result.join_type = JoinType::NATURAL;
    }

    // Right-hand side table name
    if (je->rarg != nullptr) {
        auto rarg_tag = nodeTag(je->rarg);
        if (rarg_tag == T_PGRangeVar) {
            result.table_name = ExtractRelationName(je->rarg);
            auto* rv = castNode(PGRangeVar, je->rarg);
            if (rv->alias != nullptr && rv->alias->aliasname != nullptr) {
                result.alias = std::string(rv->alias->aliasname);
            }
        }
    }

    // ON condition
    if (je->quals != nullptr) {
        result.condition = TranslateExpression(je->quals);
    }

    // USING clause
    if (je->usingClause != nullptr) {
        PGListCell* cell = nullptr;
        foreach (cell, je->usingClause) {
            result.using_columns.push_back(GetStringFromValue(lfirst(cell)));
        }
    }

    return result;
}

std::string Parser::GetStringFromValue(void* pg_value) {
    if (pg_value == nullptr) return "";

    auto tag = nodeTag(pg_value);
    auto* val = castNode(PGValue, pg_value);

    switch (tag) {
        case T_PGString:
            return std::string(strVal(val));
        case T_PGInteger:
            return std::to_string(intVal(val));
        case T_PGFloat:
            return std::string(strVal(val));
        default:
            return "";
    }
}

std::string Parser::ExtractRelationName(void* pg_range_var) {
    if (pg_range_var == nullptr) return "";

    auto* rv = castNode(PGRangeVar, pg_range_var);
    if (rv->relname != nullptr) {
        return std::string(rv->relname);
    }
    return "";
}

Value Parser::TranslateValue(void* pg_const_node) {
    if (pg_const_node == nullptr) {
        return Value();
    }

    auto tag = nodeTag(pg_const_node);

    switch (tag) {
        case T_PGInteger: {
            auto* val = castNode(PGValue, pg_const_node);
            return Value::CreateBigInt(static_cast<int64_t>(intVal(val)));
        }
        case T_PGFloat: {
            auto* val = castNode(PGValue, pg_const_node);
            return Value::CreateDecimal(atof(strVal(val)));
        }
        case T_PGString: {
            auto* val = castNode(PGValue, pg_const_node);
            return Value::CreateVarchar(std::string(strVal(val)));
        }
        case T_PGNull: {
            return Value();  // INVALID type represents NULL
        }
        case T_PGAConst: {
            auto* aconst = castNode(PGAConst, pg_const_node);
            return TranslateValue(&aconst->val);
        }
        default:
            return Value();
    }
}

}  // namespace goods_db
