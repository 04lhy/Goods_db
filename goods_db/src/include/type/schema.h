#pragma once

#include <string>
#include <vector>
#include "common/rid.h"

namespace goods_db {

/**
 * Type identifiers for the type system.
 */
enum class TypeId : uint8_t {
    INVALID = 0,
    BOOLEAN = 1,
    TINYINT = 2,
    SMALLINT = 3,
    INTEGER = 4,
    BIGINT = 5,
    DECIMAL = 6,
    TIMESTAMP = 7,
    VARCHAR = 8,
};

/** Get string name of a TypeId */
const char* TypeIdToString(TypeId type_id);

/** Get storage size for fixed-length types (returns 0 for VARCHAR) */
uint32_t TypeIdSize(TypeId type_id);

/**
 * Column definition in a schema.
 */
struct Column {
    std::string column_name;
    TypeId column_type;
    uint32_t max_length;  // For VARCHAR only; 0 for fixed-length types
    bool is_nullable;

    Column() : column_type(TypeId::INVALID), max_length(0), is_nullable(true) {}

    Column(std::string name, TypeId type, uint32_t max_len = 0,
           bool nullable = true)
        : column_name(std::move(name)),
          column_type(type),
          max_length(max_len),
          is_nullable(nullable) {}

    bool IsNullable() const { return is_nullable; }

    /** Get fixed storage size (0 for VARCHAR) */
    uint32_t GetFixedSize() const { return TypeIdSize(column_type); }

    /** Get total storage size. For VARCHAR, equals max_length */
    uint32_t GetStorageSize() const {
        if (column_type == TypeId::VARCHAR) return max_length;
        return TypeIdSize(column_type);
    }

    bool operator==(const Column& other) const {
        return column_name == other.column_name &&
               column_type == other.column_type &&
               max_length == other.max_length && is_nullable == other.is_nullable;
    }

    std::string ToString() const;
};

/**
 * Schema definition for a table.
 */
class Schema {
public:
    Schema() = default;
    explicit Schema(std::vector<Column> columns);

    /** Get number of columns */
    uint32_t GetColumnCount() const { return columns_.size(); }

    /** Get column by index */
    const Column& GetColumn(uint32_t idx) const;

    /** Get column index by name. Returns -1 if not found. */
    int32_t GetColIdx(const std::string& name) const;

    /** Get all columns */
    const std::vector<Column>& GetColumns() const { return columns_; }

    /** Check if column exists */
    bool HasColumn(const std::string& name) const;

    /** Get total fixed-length size of the schema */
    uint32_t GetFixedLengthSize() const;

    /** Serialize schema to bytes (for Catalog persistence) */
    std::string Serialize() const;

    /** Deserialize schema from bytes */
    static Schema Deserialize(const char* data, uint32_t len);

    std::string ToString() const;

private:
    std::vector<Column> columns_;
};

}  // namespace goods_db
