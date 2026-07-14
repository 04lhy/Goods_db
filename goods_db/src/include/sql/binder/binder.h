#pragma once

#include <memory>
#include <string>
#include <vector>

#include "catalog/catalog.h"
#include "sql/binder/bound_statement.h"
#include "sql/parser/ast.h"

namespace goods_db {

/**
 * Binder — performs semantic analysis on the AST.
 *
 * Responsibilities:
 *   1. Table name resolution via Catalog
 *   2. Column name resolution via Schema (name → column index)
 *   3. Type checking and derivation
 *   4. Produce BoundStatement tree from AST
 *
 * Usage:
 *   Binder binder(catalog);
 *   auto bound = binder.Bind(ast_statement);
 */
class Binder {
public:
    explicit Binder(Catalog* catalog) : catalog_(catalog) {}

    // =========================================================================
    // Top-level binding
    // =========================================================================

    /** Bind an AST statement node to a BoundStatement */
    std::unique_ptr<BoundStatement> Bind(ASTStatement* stmt);

    // =========================================================================
    // Statement binding methods
    // =========================================================================

    std::unique_ptr<BoundSelectStmt> BindSelect(SelectStatement* stmt);
    std::unique_ptr<BoundInsertStmt> BindInsert(InsertStatement* stmt);
    std::unique_ptr<BoundUpdateStmt> BindUpdate(UpdateStatement* stmt);
    std::unique_ptr<BoundDeleteStmt> BindDelete(DeleteStatement* stmt);
    std::unique_ptr<BoundCreateStmt> BindCreate(CreateStatement* stmt);
    std::unique_ptr<BoundDropStmt> BindDrop(DropStatement* stmt);
    std::unique_ptr<BoundCreateIndexStmt> BindCreateIndex(CreateIndexStatement* stmt);

    // =========================================================================
    // Expression binding
    // =========================================================================

    /** Bind an AST expression to a BoundExpression, given table schema context */
    std::unique_ptr<BoundExpression> BindExpression(ASTExpression* expr,
                                                     const Schema* schema);

    // =========================================================================
    // Error handling
    // =========================================================================

    const std::string& error_message() const { return error_message_; }

private:
    Catalog* catalog_;
    std::string error_message_;

    /** Resolve a column name to its schema index. Returns -1 if not found. */
    int32_t ResolveColumn(const std::string& name, const Schema* schema);

    /** Resolve a possibly qualified column reference (table.col or just col) */
    int32_t ResolveColumnRef(const std::string& col_name,
                              const std::string& table_name,
                              const Schema* schema);

    /** Derive result type for a binary operation */
    TypeId DeriveBinaryOpType(const std::string& op, TypeId left_type,
                               TypeId right_type);

    /** Derive result type for a unary operation */
    TypeId DeriveUnaryOpType(const std::string& op, TypeId operand_type);

    /** Expand SELECT * into column references for all schema columns */
    std::vector<std::unique_ptr<BoundExpression>> ExpandStar(
        const Schema* schema);

    /** Check if a function name is an aggregate */
    static bool IsAggregate(const std::string& name);

    /**
     * Resolve a table name from possibly database-qualified format (db.table).
     * If the name contains '.', extracts the portion after the last '.'.
     * For a single-database catalog, the db prefix is ignored.
     */
    static std::string ResolveTableName(const std::string& qualified_name);
};

}  // namespace goods_db
