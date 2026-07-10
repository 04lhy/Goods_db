#pragma once

#include <memory>
#include <string>
#include <vector>

#include "sql/parser/ast.h"

namespace goods_db {

/**
 * Parser — converts SQL text into an AST (list of ASTStatement nodes).
 *
 * Internally delegates to duckdb::PostgresParser (libpg_query) for lexing
 * and parsing, then translates the PostgreSQL parse tree into goods_db's
 * own AST representation.
 *
 * Usage:
 *   Parser parser;
 *   auto stmts = parser.Parse("SELECT * FROM t WHERE a = 1");
 *   for (auto& stmt : stmts) {
 *       fmt::print("{}\n", stmt->ToString());
 *   }
 */
class Parser {
public:
    Parser();
    ~Parser();

    // Non-copyable, non-movable (owns internal parser state)
    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&) = delete;
    Parser(Parser&&) = delete;
    Parser& operator=(Parser&&) = delete;

    /**
     * Parse a SQL string. Returns a vector of AST statement nodes.
     * Throws std::runtime_error on parse errors.
     */
    std::vector<std::unique_ptr<ASTStatement>> Parse(const std::string& sql);

    /**
     * Parse a SQL string. Clears the given result vector and fills it.
     * Returns true on success, false on parse error.
     * On failure, error_message() returns the error details.
     */
    bool TryParse(const std::string& sql,
                  std::vector<std::unique_ptr<ASTStatement>>& result);

    /** Last error message, if any */
    const std::string& error_message() const { return error_message_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string error_message_;

    // === Statement translation helpers ===
    std::unique_ptr<SelectStatement> TranslateSelectStmt(void* pg_node);
    std::unique_ptr<InsertStatement> TranslateInsertStmt(void* pg_node);
    std::unique_ptr<UpdateStatement> TranslateUpdateStmt(void* pg_node);
    std::unique_ptr<DeleteStatement> TranslateDeleteStmt(void* pg_node);
    std::unique_ptr<CreateStatement> TranslateCreateStmt(void* pg_node);
    std::unique_ptr<DropStatement> TranslateDropStmt(void* pg_node);
    std::unique_ptr<CreateIndexStatement> TranslateIndexStmt(void* pg_node);

    // === Expression translation helpers ===
    std::unique_ptr<ASTExpression> TranslateExpression(void* pg_node);
    std::unique_ptr<ASTExpression> TranslateColumnRef(void* pg_node);
    std::unique_ptr<ASTExpression> TranslateConstant(void* pg_node);
    std::unique_ptr<ASTExpression> TranslateAExpr(void* pg_node);
    std::unique_ptr<ASTExpression> TranslateBoolExpr(void* pg_node);
    std::unique_ptr<ASTExpression> TranslateFuncCall(void* pg_node);
    std::unique_ptr<ASTExpression> TranslateTypeCast(void* pg_node);
    std::unique_ptr<ASTExpression> TranslateSubLink(void* pg_node);
    std::unique_ptr<ASTExpression> TranslateCaseExpr(void* pg_node);
    std::unique_ptr<ASTExpression> TranslateNullTest(void* pg_node);
    std::unique_ptr<ASTExpression> TranslateStarExpr(void* pg_node);

    // === FROM/JOIN tree helpers ===
    void ExtractBaseTableFromJoin(void* pg_join, SelectStatement* result);
    void FlattenJoinTree(void* pg_node, SelectStatement* result);

    // === Utility helpers ===
    DataType TranslateTypeName(void* pg_type_name);
    ColumnDefinition TranslateColumnDef(void* pg_col_def);
    OrderByClause TranslateSortBy(void* pg_sort_by);
    JoinClause TranslateJoinExpr(void* pg_join);
    std::string GetStringFromValue(void* pg_value);
    std::string ExtractRelationName(void* pg_range_var);
    Value TranslateValue(void* pg_const_node);
};

}  // namespace goods_db
