#include "sql/binder/binder.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include "sql/parser/ast.h"
#include "storage/table/tuple.h"

namespace goods_db {

// =============================================================================
// BoundExpression Evaluate implementations
// =============================================================================

Value BoundColumnRef::Evaluate(const Tuple* tuple, const Schema* schema) const {
    if (col_idx < 0 || col_idx >= static_cast<int32_t>(schema->GetColumnCount())) {
        return Value();
    }
    return tuple->GetValue(schema, static_cast<uint32_t>(col_idx));
}

std::string BoundColumnRef::ToString() const {
    return "#" + std::to_string(col_idx);
}

Value BoundBinaryOp::Evaluate(const Tuple* tuple, const Schema* schema) const {
    auto lv = left->Evaluate(tuple, schema);
    auto rv = right->Evaluate(tuple, schema);

    // Handle NULL propagation
    if (lv.GetTypeId() == TypeId::INVALID || rv.GetTypeId() == TypeId::INVALID) {
        return Value();
    }

    if (op == "AND") {
        return Value::CreateBoolean(lv.GetAsBoolean() && rv.GetAsBoolean());
    }
    if (op == "OR") {
        return Value::CreateBoolean(lv.GetAsBoolean() || rv.GetAsBoolean());
    }
    if (op == "||") {
        return Value::CreateVarchar(lv.ToString() + rv.ToString());
    }

    // Arithmetic operations
    double ld = 0, rd = 0;
    bool left_int = false, right_int = false;

    switch (lv.GetTypeId()) {
        case TypeId::TINYINT:  ld = lv.GetAsTinyInt(); left_int = true; break;
        case TypeId::SMALLINT: ld = lv.GetAsSmallInt(); left_int = true; break;
        case TypeId::INTEGER:  ld = lv.GetAsInteger(); left_int = true; break;
        case TypeId::BIGINT:   ld = static_cast<double>(lv.GetAsBigInt()); left_int = true; break;
        case TypeId::DECIMAL:  ld = lv.GetAsDecimal(); break;
        default: return Value();
    }

    switch (rv.GetTypeId()) {
        case TypeId::TINYINT:  rd = rv.GetAsTinyInt(); right_int = true; break;
        case TypeId::SMALLINT: rd = rv.GetAsSmallInt(); right_int = true; break;
        case TypeId::INTEGER:  rd = rv.GetAsInteger(); right_int = true; break;
        case TypeId::BIGINT:   rd = static_cast<double>(rv.GetAsBigInt()); right_int = true; break;
        case TypeId::DECIMAL:  rd = rv.GetAsDecimal(); break;
        default: return Value();
    }

    double result = 0;
    if (op == "+") result = ld + rd;
    else if (op == "-") result = ld - rd;
    else if (op == "*") result = ld * rd;
    else if (op == "/") {
        if (rd == 0) return Value();
        result = ld / rd;
    }
    else if (op == "%") {
        if (rd == 0) return Value();
        result = static_cast<int64_t>(ld) % static_cast<int64_t>(rd);
    }
    else { return Value(); }

    if (left_int && right_int && op != "/") {
        return Value::CreateBigInt(static_cast<int64_t>(result));
    }
    return Value::CreateDecimal(result);
}

std::string BoundBinaryOp::ToString() const {
    return "(" + left->ToString() + " " + op + " " + right->ToString() + ")";
}

// Helper: coerce two Values to a common numeric type for comparison
static void CoerceToCommonType(Value& lv, Value& rv) {
    auto lt = lv.GetTypeId(), rt = rv.GetTypeId();
    if (lt == rt) return;

    // Both numeric? Promote to wider type
    auto is_numeric = [](TypeId t) {
        return t == TypeId::TINYINT || t == TypeId::SMALLINT ||
               t == TypeId::INTEGER || t == TypeId::BIGINT ||
               t == TypeId::DECIMAL;
    };

    if (is_numeric(lt) && is_numeric(rt)) {
        // Promote to DECIMAL if either is DECIMAL, else BIGINT
        if (lt == TypeId::DECIMAL || rt == TypeId::DECIMAL) {
            double ld = 0, rd = 0;
            switch (lt) {
                case TypeId::TINYINT:  ld = lv.GetAsTinyInt(); break;
                case TypeId::SMALLINT: ld = lv.GetAsSmallInt(); break;
                case TypeId::INTEGER:  ld = lv.GetAsInteger(); break;
                case TypeId::BIGINT:   ld = static_cast<double>(lv.GetAsBigInt()); break;
                case TypeId::DECIMAL:  ld = lv.GetAsDecimal(); break;
                default: break;
            }
            switch (rt) {
                case TypeId::TINYINT:  rd = rv.GetAsTinyInt(); break;
                case TypeId::SMALLINT: rd = rv.GetAsSmallInt(); break;
                case TypeId::INTEGER:  rd = rv.GetAsInteger(); break;
                case TypeId::BIGINT:   rd = static_cast<double>(rv.GetAsBigInt()); break;
                case TypeId::DECIMAL:  rd = rv.GetAsDecimal(); break;
                default: break;
            }
            lv = Value::CreateDecimal(ld);
            rv = Value::CreateDecimal(rd);
        } else {
            // Both integers → promote to BIGINT
            int64_t li = 0, ri = 0;
            switch (lt) {
                case TypeId::TINYINT:  li = lv.GetAsTinyInt(); break;
                case TypeId::SMALLINT: li = lv.GetAsSmallInt(); break;
                case TypeId::INTEGER:  li = lv.GetAsInteger(); break;
                case TypeId::BIGINT:   li = lv.GetAsBigInt(); break;
                default: break;
            }
            switch (rt) {
                case TypeId::TINYINT:  ri = rv.GetAsTinyInt(); break;
                case TypeId::SMALLINT: ri = rv.GetAsSmallInt(); break;
                case TypeId::INTEGER:  ri = rv.GetAsInteger(); break;
                case TypeId::BIGINT:   ri = rv.GetAsBigInt(); break;
                default: break;
            }
            lv = Value::CreateBigInt(li);
            rv = Value::CreateBigInt(ri);
        }
    }
}

Value BoundComparison::Evaluate(const Tuple* tuple, const Schema* schema) const {
    auto lv = left->Evaluate(tuple, schema);
    auto rv = right->Evaluate(tuple, schema);

    if (lv.GetTypeId() == TypeId::INVALID || rv.GetTypeId() == TypeId::INVALID) {
        // NULL comparison always returns false (NULL != NULL)
        return Value::CreateBoolean(false);
    }

    // Coerce types for comparison
    CoerceToCommonType(lv, rv);

    if (op == "=")  return Value::CreateBoolean(lv == rv);
    if (op == "<>") return Value::CreateBoolean(lv != rv);
    if (op == "!=") return Value::CreateBoolean(lv != rv);
    if (op == "<")  return Value::CreateBoolean(lv < rv);
    if (op == ">")  return Value::CreateBoolean(lv > rv);
    if (op == "<=") return Value::CreateBoolean(lv <= rv);
    if (op == ">=") return Value::CreateBoolean(lv >= rv);

    // LIKE operator (simple substring match)
    if (op == "~~" || op == "LIKE" || op == "like") {
        auto lstr = lv.ToString();
        auto rstr = rv.ToString();
        // Simple pattern: strip % wildcards and check substring
        std::string pattern = rstr;
        bool prefix = false, suffix = false;
        if (!pattern.empty() && pattern.front() == '%') { prefix = true; pattern.erase(0, 1); }
        if (!pattern.empty() && pattern.back() == '%') { suffix = true; pattern.pop_back(); }
        if (prefix && suffix) return Value::CreateBoolean(lstr.find(pattern) != std::string::npos);
        if (prefix) return Value::CreateBoolean(lstr.size() >= pattern.size() &&
            lstr.compare(lstr.size() - pattern.size(), pattern.size(), pattern) == 0);
        if (suffix) return Value::CreateBoolean(lstr.compare(0, pattern.size(), pattern) == 0);
        return Value::CreateBoolean(lstr == pattern);
    }

    return Value::CreateBoolean(false);
}

std::string BoundComparison::ToString() const {
    return left->ToString() + " " + op + " " + right->ToString();
}

Value BoundUnaryOp::Evaluate(const Tuple* tuple, const Schema* schema) const {
    auto val = operand->Evaluate(tuple, schema);
    if (val.GetTypeId() == TypeId::INVALID) return Value();

    if (op == "NOT") {
        return Value::CreateBoolean(!val.GetAsBoolean());
    }
    if (op == "-") {
        switch (val.GetTypeId()) {
            case TypeId::TINYINT:  return Value::CreateTinyInt(-val.GetAsTinyInt());
            case TypeId::SMALLINT: return Value::CreateSmallInt(-val.GetAsSmallInt());
            case TypeId::INTEGER:  return Value::CreateInteger(-val.GetAsInteger());
            case TypeId::BIGINT:   return Value::CreateBigInt(-val.GetAsBigInt());
            case TypeId::DECIMAL:  return Value::CreateDecimal(-val.GetAsDecimal());
            default: return Value();
        }
    }
    if (op == "IS_NULL") {
        return Value::CreateBoolean(val.GetTypeId() == TypeId::INVALID);
    }
    if (op == "IS_NOT_NULL") {
        return Value::CreateBoolean(val.GetTypeId() != TypeId::INVALID);
    }
    return Value();
}

std::string BoundUnaryOp::ToString() const {
    return op + "(" + operand->ToString() + ")";
}

// Simple function evaluation (aggregates return INVALID - evaluated by AggregationExecutor)
Value BoundFunctionCall::Evaluate(const Tuple* /*tuple*/,
                                   const Schema* /*schema*/) const {
    // Scalar functions are evaluated by specialized executors
    // Here we just return INVALID as a sentinel
    return Value();
}

TypeId BoundFunctionCall::GetReturnType() const {
    if (function_name == "COUNT") return TypeId::BIGINT;
    if (function_name == "SUM" || function_name == "AVG") return TypeId::DECIMAL;
    if (function_name == "MAX" || function_name == "MIN") return TypeId::INVALID;
    return TypeId::INVALID;
}

std::string BoundFunctionCall::ToString() const {
    std::string result = function_name + "(";
    if (is_star) {
        result += "*";
    } else {
        if (is_distinct) result += "DISTINCT ";
        for (size_t i = 0; i < arguments.size(); i++) {
            if (i > 0) result += ", ";
            result += arguments[i]->ToString();
        }
    }
    result += ")";
    return result;
}

// =============================================================================
// BoundStatement ToString implementations
// =============================================================================

std::string BoundSelectStmt::ToString() const {
    std::string result = "BOUND_SELECT [";
    for (size_t i = 0; i < select_list.size(); i++) {
        if (i > 0) result += ", ";
        result += select_list[i]->ToString();
    }
    result += "] FROM " + table_name;
    if (where_clause) result += " WHERE " + where_clause->ToString();
    return result;
}

std::string BoundInsertStmt::ToString() const {
    return "BOUND_INSERT INTO " + table_name + " (" +
           std::to_string(value_rows.size()) + " rows)";
}

std::string BoundUpdateStmt::ToString() const {
    return "BOUND_UPDATE " + table_name;
}

std::string BoundDeleteStmt::ToString() const {
    return "BOUND_DELETE FROM " + table_name;
}

std::string BoundCreateStmt::ToString() const {
    return "BOUND_CREATE TABLE " + table_name;
}

std::string BoundDropStmt::ToString() const {
    return "BOUND_DROP TABLE " + table_name;
}

std::string BoundCreateIndexStmt::ToString() const {
    return "BOUND_CREATE INDEX " + index_name + " ON " + table_name;
}

// =============================================================================
// Binder Implementation
// =============================================================================

std::unique_ptr<BoundStatement> Binder::Bind(ASTStatement* stmt) {
    error_message_.clear();

    if (!stmt) return nullptr;

    switch (stmt->GetType()) {
        case ASTNodeType::STATEMENT_SELECT:
            return BindSelect(static_cast<SelectStatement*>(stmt));
        case ASTNodeType::STATEMENT_INSERT:
            return BindInsert(static_cast<InsertStatement*>(stmt));
        case ASTNodeType::STATEMENT_UPDATE:
            return BindUpdate(static_cast<UpdateStatement*>(stmt));
        case ASTNodeType::STATEMENT_DELETE:
            return BindDelete(static_cast<DeleteStatement*>(stmt));
        case ASTNodeType::STATEMENT_CREATE:
            return BindCreate(static_cast<CreateStatement*>(stmt));
        case ASTNodeType::STATEMENT_DROP:
            return BindDrop(static_cast<DropStatement*>(stmt));
        case ASTNodeType::STATEMENT_CREATE_INDEX:
            return BindCreateIndex(static_cast<CreateIndexStatement*>(stmt));
        default:
            error_message_ = "Unknown statement type";
            return nullptr;
    }
}

// =============================================================================
// BindSelect
// =============================================================================

std::unique_ptr<BoundSelectStmt> Binder::BindSelect(SelectStatement* stmt) {
    auto result = std::make_unique<BoundSelectStmt>();

    const Schema* schema = nullptr;

    // Resolve table (optional — SELECT without FROM is allowed, e.g. SELECT 1)
    if (!stmt->table_name.empty()) {
        std::string actual_table = ResolveTableName(stmt->table_name);
        TableInfo* table_info = catalog_->GetTable(actual_table);
        if (!table_info) {
            error_message_ = "Table not found: " + actual_table;
            return nullptr;
        }
        result->table_name = actual_table;
        result->table_schema = &table_info->schema;
        schema = result->table_schema;
    } else {
        // No FROM clause — queries like "SELECT 1", "SELECT 1+2"
        result->table_name = "";
        result->table_schema = nullptr;
        schema = nullptr;
    }

    // Bind select list
    for (auto& expr : stmt->select_list) {
        auto bound = BindExpression(expr.get(), schema);
        if (bound) {
            bound->alias = expr->alias;
            result->select_list.push_back(std::move(bound));
        }
    }

    result->is_distinct = stmt->is_distinct;

    // Bind WHERE clause
    if (stmt->where_clause) {
        result->where_clause = BindExpression(stmt->where_clause.get(), schema);
    }

    // Bind GROUP BY
    for (auto& expr : stmt->group_by) {
        auto bound = BindExpression(expr.get(), schema);
        if (bound) result->group_by.push_back(std::move(bound));
    }

    // Bind HAVING
    if (stmt->having_clause) {
        result->having_clause = BindExpression(stmt->having_clause.get(), schema);
    }

    // Bind ORDER BY
    for (auto& ob : stmt->order_by) {
        auto bound = BindExpression(ob.expression.get(), schema);
        if (bound) {
            BoundSelectStmt::OrderByItem item;
            item.expression = std::move(bound);
            item.is_asc = (ob.order_type != OrderType::DESC);
            result->order_by.push_back(std::move(item));
        }
    }

    // Bind LIMIT / OFFSET
    if (stmt->limit_count) {
        auto val = BindExpression(stmt->limit_count.get(), schema);
        if (val) {
            auto const_val = val->Evaluate(nullptr, nullptr);
            result->limit = const_val.GetAsBigInt();
        }
    }
    if (stmt->limit_offset) {
        auto val = BindExpression(stmt->limit_offset.get(), schema);
        if (val) {
            auto const_val = val->Evaluate(nullptr, nullptr);
            result->offset = const_val.GetAsBigInt();
        }
    }

    return result;
}

// =============================================================================
// BindInsert
// =============================================================================

std::unique_ptr<BoundInsertStmt> Binder::BindInsert(InsertStatement* stmt) {
    auto result = std::make_unique<BoundInsertStmt>();

    std::string actual_table = ResolveTableName(stmt->table_name);
    TableInfo* table_info = catalog_->GetTable(actual_table);
    if (!table_info) {
        error_message_ = "Table not found: " + actual_table;
        return nullptr;
    }
    result->table_name = actual_table;
    result->table_schema = &table_info->schema;

    const Schema* schema = result->table_schema;

    // Resolve column indices
    if (!stmt->column_names.empty()) {
        for (auto& col_name : stmt->column_names) {
            int32_t idx = ResolveColumn(col_name, schema);
            if (idx < 0) {
                error_message_ = "Column not found: " + col_name;
                return nullptr;
            }
            result->column_indices.push_back(idx);
        }
    } else {
        // No explicit columns: use all columns in order
        for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
            result->column_indices.push_back(static_cast<int32_t>(i));
        }
    }

    // Bind value rows
    for (auto& row : stmt->value_rows) {
        std::vector<std::unique_ptr<BoundExpression>> bound_row;
        for (auto& val : row) {
            auto bound = BindExpression(val.get(), schema);
            if (bound) bound_row.push_back(std::move(bound));
        }
        result->value_rows.push_back(std::move(bound_row));
    }

    return result;
}

// =============================================================================
// BindUpdate
// =============================================================================

std::unique_ptr<BoundUpdateStmt> Binder::BindUpdate(UpdateStatement* stmt) {
    auto result = std::make_unique<BoundUpdateStmt>();

    std::string actual_table = ResolveTableName(stmt->table_name);
    TableInfo* table_info = catalog_->GetTable(actual_table);
    if (!table_info) {
        error_message_ = "Table not found: " + actual_table;
        return nullptr;
    }
    result->table_name = actual_table;
    result->table_schema = &table_info->schema;

    const Schema* schema = result->table_schema;

    // Bind SET clauses
    for (auto& sc : stmt->set_clauses) {
        int32_t col_idx = ResolveColumn(sc.column_name, schema);
        if (col_idx < 0) {
            error_message_ = "Column not found: " + sc.column_name;
            return nullptr;
        }
        BoundUpdateStmt::SetClause bound_sc;
        bound_sc.col_idx = col_idx;
        bound_sc.value = BindExpression(sc.value.get(), schema);
        result->set_clauses.push_back(std::move(bound_sc));
    }

    // Bind WHERE
    if (stmt->where_clause) {
        result->where_clause = BindExpression(stmt->where_clause.get(), schema);
    }

    return result;
}

// =============================================================================
// BindDelete
// =============================================================================

std::unique_ptr<BoundDeleteStmt> Binder::BindDelete(DeleteStatement* stmt) {
    auto result = std::make_unique<BoundDeleteStmt>();

    std::string actual_table = ResolveTableName(stmt->table_name);
    TableInfo* table_info = catalog_->GetTable(actual_table);
    if (!table_info) {
        error_message_ = "Table not found: " + actual_table;
        return nullptr;
    }
    result->table_name = actual_table;
    result->table_schema = &table_info->schema;

    if (stmt->where_clause) {
        result->where_clause = BindExpression(stmt->where_clause.get(),
                                               result->table_schema);
    }

    return result;
}

// =============================================================================
// BindCreate
// =============================================================================

std::unique_ptr<BoundCreateStmt> Binder::BindCreate(CreateStatement* stmt) {
    auto result = std::make_unique<BoundCreateStmt>();
    result->table_name = stmt->table_name;
    result->if_not_exists = stmt->if_not_exists;

    // Convert AST column definitions to Schema columns
    std::vector<Column> columns;
    for (auto& col_def : stmt->columns) {
        Column col;
        col.column_name = col_def.column_name;
        col.column_type = col_def.data_type.ToTypeId();
        col.max_length = col_def.data_type.length;
        col.is_nullable = col_def.is_nullable;
        columns.push_back(std::move(col));
    }
    result->schema = Schema(std::move(columns));

    return result;
}

// =============================================================================
// BindDrop
// =============================================================================

std::unique_ptr<BoundDropStmt> Binder::BindDrop(DropStatement* stmt) {
    auto result = std::make_unique<BoundDropStmt>();
    result->table_name = stmt->table_name;
    result->if_exists = stmt->if_exists;
    return result;
}

// =============================================================================
// BindCreateIndex
// =============================================================================

std::unique_ptr<BoundCreateIndexStmt> Binder::BindCreateIndex(
    CreateIndexStatement* stmt) {
    auto result = std::make_unique<BoundCreateIndexStmt>();

    std::string actual_table = ResolveTableName(stmt->table_name);
    TableInfo* table_info = catalog_->GetTable(actual_table);
    if (!table_info) {
        error_message_ = "Table not found: " + actual_table;
        return nullptr;
    }

    result->index_name = stmt->index_name;
    result->table_name = actual_table;
    result->table_schema = &table_info->schema;
    result->index_type = stmt->index_type.empty() ? "btree" : stmt->index_type;
    result->is_unique = stmt->is_unique;

    int32_t col_idx = ResolveColumn(stmt->column_name, result->table_schema);
    if (col_idx < 0) {
        error_message_ = "Column not found: " + stmt->column_name;
        return nullptr;
    }
    result->col_idx = col_idx;

    return result;
}

// =============================================================================
// Expression Binding
// =============================================================================

std::unique_ptr<BoundExpression> Binder::BindExpression(ASTExpression* expr,
                                                          const Schema* schema) {
    if (!expr) return nullptr;

    switch (expr->GetType()) {
        case ASTNodeType::EXPRESSION_COLUMN_REF: {
            auto* col_ref = static_cast<ColumnRefExpression*>(expr);
            // Handle star references (e.g., *, table.*)
            if (col_ref->is_star) {
                auto result = std::make_unique<BoundStar>();
                result->alias = expr->alias;
                return result;
            }
            int32_t idx = ResolveColumnRef(col_ref->column_name,
                                            col_ref->table_name, schema);
            if (idx < 0) {
                error_message_ = "Column not found: " + col_ref->column_name;
                return nullptr;
            }
            auto result = std::make_unique<BoundColumnRef>(
                idx, schema->GetColumn(static_cast<uint32_t>(idx)).column_type);
            result->alias = expr->alias;
            return result;
        }

        case ASTNodeType::EXPRESSION_CONSTANT: {
            auto* c = static_cast<ConstantExpression*>(expr);
            auto result = std::make_unique<BoundConstant>(c->value);
            result->alias = expr->alias;
            return result;
        }

        case ASTNodeType::EXPRESSION_BINARY_OP: {
            auto* bop = static_cast<BinaryOpExpression*>(expr);
            auto result = std::make_unique<BoundBinaryOp>();
            result->op = bop->op;
            result->left = BindExpression(bop->left.get(), schema);
            result->right = BindExpression(bop->right.get(), schema);
            if (result->left && result->right) {
                result->result_type = DeriveBinaryOpType(
                    result->op, result->left->GetReturnType(),
                    result->right->GetReturnType());
            }
            result->alias = expr->alias;
            return result;
        }

        case ASTNodeType::EXPRESSION_UNARY_OP: {
            auto* uop = static_cast<UnaryOpExpression*>(expr);
            auto result = std::make_unique<BoundUnaryOp>();
            result->op = uop->op;
            result->operand = BindExpression(uop->operand.get(), schema);
            if (result->operand) {
                result->result_type = DeriveUnaryOpType(
                    result->op, result->operand->GetReturnType());
            }
            result->alias = expr->alias;
            return result;
        }

        case ASTNodeType::EXPRESSION_COMPARISON: {
            auto* cmp = static_cast<ComparisonExpression*>(expr);
            auto result = std::make_unique<BoundComparison>();
            result->op = cmp->op;
            result->left = BindExpression(cmp->left.get(), schema);
            result->right = BindExpression(cmp->right.get(), schema);
            result->alias = expr->alias;
            return result;
        }

        case ASTNodeType::EXPRESSION_FUNCTION_CALL: {
            auto* fc = static_cast<FunctionCallExpression*>(expr);
            auto result = std::make_unique<BoundFunctionCall>();
            result->function_name = fc->function_name;
            result->is_distinct = fc->is_distinct;
            result->is_star = fc->is_star;
            for (auto& arg : fc->arguments) {
                auto bound_arg = BindExpression(arg.get(), schema);
                if (bound_arg) result->arguments.push_back(std::move(bound_arg));
            }
            result->alias = expr->alias;
            return result;
        }

        case ASTNodeType::EXPRESSION_STAR: {
            auto result = std::make_unique<BoundStar>();
            result->alias = expr->alias;
            return result;
        }

        case ASTNodeType::EXPRESSION_IS_NULL: {
            auto* isn = static_cast<IsNullExpression*>(expr);
            auto result = std::make_unique<BoundUnaryOp>();
            result->op = isn->is_not ? "IS_NOT_NULL" : "IS_NULL";
            result->operand = BindExpression(isn->operand.get(), schema);
            if (result->operand) {
                result->result_type = TypeId::BOOLEAN;
            }
            result->alias = expr->alias;
            return result;
        }

        case ASTNodeType::EXPRESSION_IN_LIST: {
            // Transform IN (a, b, c) to col = a OR col = b OR col = c
            auto* in_expr = static_cast<InListExpression*>(expr);
            auto left = BindExpression(in_expr->left.get(), schema);
            if (!left || in_expr->in_list.empty()) return nullptr;

            std::unique_ptr<BoundExpression> result;
            for (auto& item : in_expr->in_list) {
                auto cmp = std::make_unique<BoundComparison>();
                cmp->op = in_expr->is_not ? "<>" : "=";
                // Clone left for each comparison (simplified: reuse schema info)
                cmp->left = BindExpression(in_expr->left.get(), schema);
                cmp->right = BindExpression(item.get(), schema);

                if (!result) {
                    result = std::move(cmp);
                } else {
                    auto or_op = std::make_unique<BoundBinaryOp>();
                    or_op->op = in_expr->is_not ? "AND" : "OR";
                    or_op->left = std::move(result);
                    or_op->right = std::move(cmp);
                    or_op->result_type = TypeId::BOOLEAN;
                    result = std::move(or_op);
                }
            }
            return result;
        }

        case ASTNodeType::EXPRESSION_TYPE_CAST:
        case ASTNodeType::EXPRESSION_SUBQUERY:
        case ASTNodeType::EXPRESSION_CASE:
        default:
            // For unsupported expression types, return a placeholder
            return nullptr;
    }
}

// =============================================================================
// Column Resolution
// =============================================================================

int32_t Binder::ResolveColumn(const std::string& name, const Schema* schema) {
    return schema->GetColIdx(name);
}

int32_t Binder::ResolveColumnRef(const std::string& col_name,
                                   const std::string& /*table_name*/,
                                   const Schema* schema) {
    // For now, ignore table qualifier and just search by column name
    return schema->GetColIdx(col_name);
}

// =============================================================================
// Type Derivation
// =============================================================================

TypeId Binder::DeriveBinaryOpType(const std::string& op,
                                    TypeId left_type, TypeId right_type) {
    if (op == "AND" || op == "OR") return TypeId::BOOLEAN;

    // Arithmetic: promote to wider type
    if (left_type == TypeId::DECIMAL || right_type == TypeId::DECIMAL)
        return TypeId::DECIMAL;
    if (left_type == TypeId::BIGINT || right_type == TypeId::BIGINT)
        return TypeId::BIGINT;
    if (left_type == TypeId::INTEGER || right_type == TypeId::INTEGER)
        return TypeId::INTEGER;
    return left_type;
}

TypeId Binder::DeriveUnaryOpType(const std::string& op, TypeId operand_type) {
    if (op == "NOT" || op == "IS_NULL" || op == "IS_NOT_NULL")
        return TypeId::BOOLEAN;
    return operand_type;
}

bool Binder::IsAggregate(const std::string& name) {
    return name == "COUNT" || name == "SUM" || name == "AVG" ||
           name == "MAX" || name == "MIN" || name == "COUNT_DISTINCT";
}

std::string Binder::ResolveTableName(const std::string& qualified_name) {
    size_t dot = qualified_name.find('.');
    if (dot != std::string::npos) {
        // Strip db prefix — table name is everything after the last '.'
        return qualified_name.substr(dot + 1);
    }
    return qualified_name;
}

}  // namespace goods_db
