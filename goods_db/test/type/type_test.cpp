#include <gtest/gtest.h>
#include "test/common/test_common.h"
#include "type/schema.h"
#include "type/value.h"

using namespace goods_db;

TEST(ValueTest, CreateAndCompare) {
    auto v1 = Value::CreateInteger(42);
    auto v2 = Value::CreateInteger(42);
    auto v3 = Value::CreateInteger(100);

    EXPECT_EQ(v1, v2);
    EXPECT_NE(v1, v3);
    EXPECT_LT(v1, v3);
    EXPECT_EQ(v1.GetTypeId(), TypeId::INTEGER);
    EXPECT_EQ(v1.GetAsInteger(), 42);
}

TEST(ValueTest, VarcharCreateAndCompare) {
    auto v1 = Value::CreateVarchar("hello");
    auto v2 = Value::CreateVarchar("hello");
    auto v3 = Value::CreateVarchar("world");

    EXPECT_EQ(v1, v2);
    EXPECT_NE(v1, v3);
    EXPECT_EQ(v1.GetAsVarchar(), "hello");
}

TEST(ValueTest, SerializeDeserialize) {
    auto v = Value::CreateInteger(12345);
    char buf[8];
    v.SerializeTo(buf);

    auto v2 = Value();
    v2.DeserializeFrom(buf, TypeId::INTEGER);
    EXPECT_EQ(v, v2);
}

TEST(ValueTest, ToString) {
    EXPECT_EQ(Value::CreateBoolean(true).ToString(), "true");
    EXPECT_EQ(Value::CreateInteger(42).ToString(), "42");
    EXPECT_EQ(Value::CreateVarchar("test").ToString(), "'test'");
}

TEST(SchemaTest, CreateAndQuery) {
    std::vector<Column> cols;
    cols.emplace_back("id", TypeId::INTEGER);
    cols.emplace_back("name", TypeId::VARCHAR, 64);
    cols.emplace_back("age", TypeId::SMALLINT);

    Schema schema(std::move(cols));
    EXPECT_EQ(schema.GetColumnCount(), 3u);
    EXPECT_EQ(schema.GetColIdx("id"), 0);
    EXPECT_EQ(schema.GetColIdx("name"), 1);
    EXPECT_EQ(schema.GetColIdx("age"), 2);
    EXPECT_EQ(schema.GetColIdx("nonexistent"), -1);
    EXPECT_TRUE(schema.HasColumn("id"));
    EXPECT_FALSE(schema.HasColumn("nonexistent"));
}

TEST(SchemaTest, SerializeDeserialize) {
    std::vector<Column> cols;
    cols.emplace_back("id", TypeId::INTEGER);
    cols.emplace_back("name", TypeId::VARCHAR, 64);

    Schema schema(std::move(cols));
    auto data = schema.Serialize();
    auto deserialized = Schema::Deserialize(data.data(), data.size());

    EXPECT_EQ(deserialized.GetColumnCount(), 2u);
    EXPECT_EQ(deserialized.GetColIdx("id"), 0);
    EXPECT_EQ(deserialized.GetColIdx("name"), 1);
    EXPECT_EQ(deserialized.GetColumn(0).column_type, TypeId::INTEGER);
    EXPECT_EQ(deserialized.GetColumn(1).column_type, TypeId::VARCHAR);
    EXPECT_EQ(deserialized.GetColumn(1).max_length, 64u);
}

TEST(TupleTest, CreateFromValues) {
    auto schema = goods_db::test::CreateTestSchema();
    auto values = goods_db::test::CreateTestValues(1, "Test Product", 9.99);

    auto tuple = Tuple::CreateFromValues(values, &schema);

    EXPECT_EQ(tuple.GetValue(&schema, 0).GetAsInteger(), 1);
    EXPECT_EQ(tuple.GetValue(&schema, 1).GetAsVarchar(), "Test Product");
    EXPECT_DOUBLE_EQ(tuple.GetValue(&schema, 2).GetAsDecimal(), 9.99);
}
