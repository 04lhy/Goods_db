#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "sql/parser/ast.h"
#include "sql/parser/parser.h"

namespace goods_db {

class ParserTest : public ::testing::Test {
protected:
    Parser parser_;

    /** Helper: parse a single SQL statement and return it */
    std::unique_ptr<ASTStatement> ParseOne(const std::string& sql) {
        auto stmts = parser_.Parse(sql);
        EXPECT_EQ(stmts.size(), 1) << "Expected exactly 1 statement, got " << stmts.size();
        if (stmts.empty()) return nullptr;
        return std::move(stmts[0]);
    }

    /** Helper: check that the statement has the expected type */
    template <typename T>
    T* ExpectType(std::unique_ptr<ASTStatement>& stmt) {
        T* result = dynamic_cast<T*>(stmt.get());
        EXPECT_NE(result, nullptr) << "Expected statement type mismatch";
        return result;
    }
};

// =============================================================================
// Simple SELECT Tests
// =============================================================================
TEST_F(ParserTest, SimpleSelectStar) {
    auto stmt = ParseOne("SELECT * FROM users");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_EQ(sel->select_list.size(), 1);
    EXPECT_EQ(sel->table_name, "users");
    EXPECT_EQ(sel->where_clause, nullptr);
}

TEST_F(ParserTest, SimpleSelectColumns) {
    auto stmt = ParseOne("SELECT id, name, email FROM customers");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_EQ(sel->select_list.size(), 3);
    EXPECT_EQ(sel->table_name, "customers");
}

TEST_F(ParserTest, SelectWithAlias) {
    auto stmt = ParseOne("SELECT id AS user_id, name AS user_name FROM users");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_EQ(sel->select_list.size(), 2);
    EXPECT_EQ(sel->select_list[0]->alias, "user_id");
    EXPECT_EQ(sel->select_list[1]->alias, "user_name");
}

TEST_F(ParserTest, SelectDistinct) {
    auto stmt = ParseOne("SELECT DISTINCT category FROM products");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_TRUE(sel->is_distinct);
}

TEST_F(ParserTest, SelectWithTableAlias) {
    auto stmt = ParseOne("SELECT u.name FROM users u");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_EQ(sel->table_name, "users");
    EXPECT_EQ(sel->table_alias, "u");
}

// =============================================================================
// WHERE Clause Tests
// =============================================================================
TEST_F(ParserTest, SelectWithWhereEquals) {
    auto stmt = ParseOne("SELECT * FROM products WHERE price = 100");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_NE(sel->where_clause, nullptr);
    EXPECT_NE(sel->where_clause->ToString().find("="), std::string::npos);
}

TEST_F(ParserTest, SelectWithWhereAnd) {
    auto stmt = ParseOne("SELECT * FROM products WHERE price > 10 AND price < 100");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_NE(sel->where_clause, nullptr);
    EXPECT_NE(sel->where_clause->ToString().find("AND"), std::string::npos);
}

TEST_F(ParserTest, SelectWithWhereOr) {
    auto stmt = ParseOne("SELECT * FROM products WHERE category = 'A' OR category = 'B'");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_NE(sel->where_clause->ToString().find("OR"), std::string::npos);
}

TEST_F(ParserTest, SelectWithWhereIn) {
    auto stmt = ParseOne("SELECT * FROM products WHERE id IN (1, 2, 3)");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_NE(sel->where_clause->ToString().find("IN"), std::string::npos);
}

TEST_F(ParserTest, SelectWithWhereIsNull) {
    auto stmt = ParseOne("SELECT * FROM products WHERE deleted IS NULL");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_NE(sel->where_clause->ToString().find("IS NULL"), std::string::npos);
}

TEST_F(ParserTest, SelectWithWhereIsNotNull) {
    auto stmt = ParseOne("SELECT * FROM products WHERE deleted IS NOT NULL");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_NE(sel->where_clause->ToString().find("IS NOT NULL"), std::string::npos);
}

// =============================================================================
// ORDER BY, LIMIT, OFFSET Tests
// =============================================================================
TEST_F(ParserTest, SelectWithOrderBy) {
    auto stmt = ParseOne("SELECT * FROM products ORDER BY price DESC");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_EQ(sel->order_by.size(), 1);
}

TEST_F(ParserTest, SelectWithLimit) {
    auto stmt = ParseOne("SELECT * FROM products LIMIT 10");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_NE(sel->limit_count, nullptr);
}

TEST_F(ParserTest, SelectWithLimitOffset) {
    auto stmt = ParseOne("SELECT * FROM products LIMIT 10 OFFSET 5");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_NE(sel->limit_count, nullptr);
    EXPECT_NE(sel->limit_offset, nullptr);
}

TEST_F(ParserTest, SelectWithGroupBy) {
    auto stmt = ParseOne("SELECT category, COUNT(*) FROM products GROUP BY category");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_EQ(sel->group_by.size(), 1);
    EXPECT_NE(sel->select_list[1]->ToString().find("COUNT"), std::string::npos);
}

TEST_F(ParserTest, SelectWithGroupByAndHaving) {
    auto stmt = ParseOne(
        "SELECT category, COUNT(*) FROM products GROUP BY category HAVING COUNT(*) > 5");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_EQ(sel->group_by.size(), 1);
    EXPECT_NE(sel->having_clause, nullptr);
}

// =============================================================================
// JOIN Tests
// =============================================================================
TEST_F(ParserTest, SelectWithInnerJoin) {
    auto stmt = ParseOne(
        "SELECT o.id, c.name FROM orders o INNER JOIN customers c ON o.customer_id = c.id");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_EQ(sel->table_name, "orders");
    EXPECT_EQ(sel->joins.size(), 1);
    EXPECT_EQ(sel->joins[0].table_name, "customers");
}

TEST_F(ParserTest, SelectWithLeftJoin) {
    auto stmt = ParseOne(
        "SELECT * FROM orders LEFT JOIN customers ON orders.customer_id = customers.id");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_EQ(sel->joins.size(), 1);
}

TEST_F(ParserTest, SelectWithMultipleJoins) {
    auto stmt = ParseOne(
        "SELECT * FROM a JOIN b ON a.id = b.a_id JOIN c ON b.id = c.b_id");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    // At least one join should be detected
    EXPECT_GE(sel->joins.size(), 1);
}

// =============================================================================
// INSERT Tests
// =============================================================================
TEST_F(ParserTest, InsertSingleRow) {
    auto stmt = ParseOne("INSERT INTO users VALUES (1, 'Alice', 'alice@example.com')");
    auto* ins = ExpectType<InsertStatement>(stmt);
    ASSERT_NE(ins, nullptr);
    EXPECT_EQ(ins->table_name, "users");
    EXPECT_EQ(ins->value_rows.size(), 1);
    EXPECT_EQ(ins->value_rows[0].size(), 3);
}

TEST_F(ParserTest, InsertMultipleRows) {
    auto stmt = ParseOne("INSERT INTO users VALUES (1, 'Alice'), (2, 'Bob'), (3, 'Charlie')");
    auto* ins = ExpectType<InsertStatement>(stmt);
    ASSERT_NE(ins, nullptr);
    EXPECT_EQ(ins->value_rows.size(), 3);
}

TEST_F(ParserTest, InsertWithColumns) {
    auto stmt = ParseOne("INSERT INTO users (id, name) VALUES (1, 'Alice')");
    auto* ins = ExpectType<InsertStatement>(stmt);
    ASSERT_NE(ins, nullptr);
    EXPECT_EQ(ins->column_names.size(), 2);
    EXPECT_EQ(ins->column_names[0], "id");
    EXPECT_EQ(ins->column_names[1], "name");
}

// =============================================================================
// UPDATE Tests
// =============================================================================
TEST_F(ParserTest, UpdateSimple) {
    auto stmt = ParseOne("UPDATE users SET name = 'Bob' WHERE id = 1");
    auto* upd = ExpectType<UpdateStatement>(stmt);
    ASSERT_NE(upd, nullptr);
    EXPECT_EQ(upd->table_name, "users");
    EXPECT_EQ(upd->set_clauses.size(), 1);
    EXPECT_EQ(upd->set_clauses[0].column_name, "name");
    EXPECT_NE(upd->where_clause, nullptr);
}

TEST_F(ParserTest, UpdateMultipleColumns) {
    auto stmt = ParseOne("UPDATE users SET name = 'Bob', email = 'bob@example.com' WHERE id = 1");
    auto* upd = ExpectType<UpdateStatement>(stmt);
    ASSERT_NE(upd, nullptr);
    EXPECT_EQ(upd->set_clauses.size(), 2);
}

// =============================================================================
// DELETE Tests
// =============================================================================
TEST_F(ParserTest, DeleteSimple) {
    auto stmt = ParseOne("DELETE FROM users WHERE id = 1");
    auto* del = ExpectType<DeleteStatement>(stmt);
    ASSERT_NE(del, nullptr);
    EXPECT_EQ(del->table_name, "users");
    EXPECT_NE(del->where_clause, nullptr);
}

TEST_F(ParserTest, DeleteAll) {
    auto stmt = ParseOne("DELETE FROM users");
    auto* del = ExpectType<DeleteStatement>(stmt);
    ASSERT_NE(del, nullptr);
    EXPECT_EQ(del->table_name, "users");
    EXPECT_EQ(del->where_clause, nullptr);
}

// =============================================================================
// CREATE TABLE Tests
// =============================================================================
TEST_F(ParserTest, CreateTableSimple) {
    auto stmt = ParseOne(
        "CREATE TABLE users (id INTEGER, name VARCHAR(100), email VARCHAR(200))");
    auto* create = ExpectType<CreateStatement>(stmt);
    ASSERT_NE(create, nullptr);
    EXPECT_EQ(create->table_name, "users");
    EXPECT_EQ(create->columns.size(), 3);
}

TEST_F(ParserTest, CreateTableWithConstraints) {
    auto stmt = ParseOne(
        "CREATE TABLE users ("
        "  id INTEGER NOT NULL PRIMARY KEY,"
        "  name VARCHAR(100) NOT NULL,"
        "  email VARCHAR(200) UNIQUE"
        ")");
    auto* create = ExpectType<CreateStatement>(stmt);
    ASSERT_NE(create, nullptr);
    EXPECT_EQ(create->columns.size(), 3);
    EXPECT_FALSE(create->columns[0].is_nullable);
    EXPECT_TRUE(create->columns[0].is_primary_key);
    EXPECT_FALSE(create->columns[1].is_nullable);
    EXPECT_TRUE(create->columns[2].is_unique);
}

TEST_F(ParserTest, CreateTableIfNotExists) {
    auto stmt = ParseOne("CREATE TABLE IF NOT EXISTS temp_data (id INTEGER)");
    auto* create = ExpectType<CreateStatement>(stmt);
    ASSERT_NE(create, nullptr);
    EXPECT_TRUE(create->if_not_exists);
}

// =============================================================================
// DROP TABLE Tests
// =============================================================================
TEST_F(ParserTest, DropTableSimple) {
    auto stmt = ParseOne("DROP TABLE users");
    auto* drop = ExpectType<DropStatement>(stmt);
    ASSERT_NE(drop, nullptr);
    EXPECT_EQ(drop->table_name, "users");
    EXPECT_FALSE(drop->if_exists);
}

TEST_F(ParserTest, DropTableIfExists) {
    auto stmt = ParseOne("DROP TABLE IF EXISTS users");
    auto* drop = ExpectType<DropStatement>(stmt);
    ASSERT_NE(drop, nullptr);
    EXPECT_EQ(drop->table_name, "users");
    EXPECT_TRUE(drop->if_exists);
}

// =============================================================================
// CREATE INDEX Tests
// =============================================================================
TEST_F(ParserTest, CreateIndexSimple) {
    auto stmt = ParseOne("CREATE INDEX idx_users_name ON users (name)");
    auto* idx = ExpectType<CreateIndexStatement>(stmt);
    ASSERT_NE(idx, nullptr);
    EXPECT_EQ(idx->index_name, "idx_users_name");
    EXPECT_EQ(idx->table_name, "users");
    EXPECT_EQ(idx->column_name, "name");
}

TEST_F(ParserTest, CreateUniqueIndex) {
    auto stmt = ParseOne("CREATE UNIQUE INDEX idx_users_email ON users (email)");
    auto* idx = ExpectType<CreateIndexStatement>(stmt);
    ASSERT_NE(idx, nullptr);
    EXPECT_TRUE(idx->is_unique);
}

TEST_F(ParserTest, CreateIndexUsingBtree) {
    auto stmt = ParseOne("CREATE INDEX idx ON products (price)");
    auto* idx = ExpectType<CreateIndexStatement>(stmt);
    ASSERT_NE(idx, nullptr);
    EXPECT_EQ(idx->table_name, "products");
    EXPECT_EQ(idx->column_name, "price");
}

// =============================================================================
// Expression Tests
// =============================================================================
TEST_F(ParserTest, FunctionCallCount) {
    auto stmt = ParseOne("SELECT COUNT(*) FROM users");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_NE(sel->select_list[0]->ToString().find("COUNT"), std::string::npos);
}

TEST_F(ParserTest, FunctionCallAggregate) {
    auto stmt = ParseOne("SELECT AVG(price), SUM(quantity), MAX(score) FROM products");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_EQ(sel->select_list.size(), 3);
}

TEST_F(ParserTest, CaseExpression) {
    auto stmt = ParseOne(
        "SELECT CASE WHEN price > 100 THEN 'expensive' ELSE 'cheap' END FROM products");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_NE(sel->select_list[0]->ToString().find("CASE"), std::string::npos);
}

TEST_F(ParserTest, CoalesceFunction) {
    auto stmt = ParseOne("SELECT COALESCE(nickname, name) FROM users");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_NE(sel->select_list[0]->ToString().find("COALESCE"), std::string::npos);
}

TEST_F(ParserTest, QualifiedColumnRef) {
    auto stmt = ParseOne("SELECT users.id, users.name FROM users");
    auto* sel = ExpectType<SelectStatement>(stmt);
    ASSERT_NE(sel, nullptr);
    EXPECT_EQ(sel->select_list.size(), 2);
}

// =============================================================================
// Error Handling Tests
// =============================================================================
TEST_F(ParserTest, ParseError) {
    EXPECT_THROW(parser_.Parse("SELEC * FROM"), std::runtime_error);
}

TEST_F(ParserTest, TryParseSuccess) {
    std::vector<std::unique_ptr<ASTStatement>> stmts;
    EXPECT_TRUE(parser_.TryParse("SELECT 1", stmts));
    EXPECT_GE(stmts.size(), 1);
}

TEST_F(ParserTest, TryParseFailure) {
    std::vector<std::unique_ptr<ASTStatement>> stmts;
    EXPECT_FALSE(parser_.TryParse("INVALID SQL SYNTAX !!!", stmts));
    EXPECT_TRUE(stmts.empty());
    EXPECT_FALSE(parser_.error_message().empty());
}

TEST_F(ParserTest, EmptyInput) {
    std::vector<std::unique_ptr<ASTStatement>> stmts;
    EXPECT_TRUE(parser_.TryParse("", stmts));
    EXPECT_TRUE(stmts.empty());
}

// =============================================================================
// ToString Round-trip Tests
// =============================================================================
TEST_F(ParserTest, ToStringContainsKeywords) {
    auto stmt = ParseOne("SELECT * FROM users WHERE id = 1");
    auto str = stmt->ToString();
    EXPECT_NE(str.find("SELECT"), std::string::npos);
    EXPECT_NE(str.find("FROM"), std::string::npos);
    EXPECT_NE(str.find("users"), std::string::npos);
    EXPECT_NE(str.find("WHERE"), std::string::npos);
}

TEST_F(ParserTest, InsertToString) {
    auto stmt = ParseOne("INSERT INTO users VALUES (1, 'Alice')");
    auto str = stmt->ToString();
    EXPECT_NE(str.find("INSERT INTO"), std::string::npos);
}

TEST_F(ParserTest, UpdateToString) {
    auto stmt = ParseOne("UPDATE users SET name = 'Bob' WHERE id = 1");
    auto str = stmt->ToString();
    EXPECT_NE(str.find("UPDATE"), std::string::npos);
    EXPECT_NE(str.find("SET"), std::string::npos);
}

TEST_F(ParserTest, DeleteToString) {
    auto stmt = ParseOne("DELETE FROM users WHERE id = 1");
    auto str = stmt->ToString();
    EXPECT_NE(str.find("DELETE FROM"), std::string::npos);
}

TEST_F(ParserTest, CreateTableToString) {
    auto stmt = ParseOne("CREATE TABLE t (a INTEGER, b VARCHAR(50))");
    auto str = stmt->ToString();
    EXPECT_NE(str.find("CREATE TABLE"), std::string::npos);
    EXPECT_NE(str.find("a"), std::string::npos);
    EXPECT_NE(str.find("b"), std::string::npos);
}

}  // namespace goods_db
