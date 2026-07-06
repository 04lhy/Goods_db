#include "type/schema.h"
#include <cstring>
#include <sstream>
#include "common/logger.h"

namespace goods_db {

const char* TypeIdToString(TypeId type_id) {
    switch (type_id) {
        case TypeId::INVALID:   return "INVALID";
        case TypeId::BOOLEAN:   return "BOOLEAN";
        case TypeId::TINYINT:   return "TINYINT";
        case TypeId::SMALLINT:  return "SMALLINT";
        case TypeId::INTEGER:   return "INTEGER";
        case TypeId::BIGINT:    return "BIGINT";
        case TypeId::DECIMAL:   return "DECIMAL";
        case TypeId::TIMESTAMP: return "TIMESTAMP";
        case TypeId::VARCHAR:   return "VARCHAR";
        default: return "UNKNOWN";
    }
}

uint32_t TypeIdSize(TypeId type_id) {
    switch (type_id) {
        case TypeId::BOOLEAN:   return 1;
        case TypeId::TINYINT:   return 1;
        case TypeId::SMALLINT:  return 2;
        case TypeId::INTEGER:   return 4;
        case TypeId::BIGINT:    return 8;
        case TypeId::DECIMAL:   return 8;
        case TypeId::TIMESTAMP: return 8;
        case TypeId::VARCHAR:   return 0;  // variable length
        case TypeId::INVALID:
        default: return 0;
    }
}

std::string Column::ToString() const {
    std::ostringstream oss;
    oss << column_name << ":" << TypeIdToString(column_type);
    if (column_type == TypeId::VARCHAR) {
        oss << "(" << max_length << ")";
    }
    if (is_nullable) oss << " NULL";
    else oss << " NOT NULL";
    return oss.str();
}

Schema::Schema(std::vector<Column> columns) : columns_(std::move(columns)) {}

const Column& Schema::GetColumn(uint32_t idx) const {
    GOODS_DB_ASSERT(idx < columns_.size(), "Column index out of range");
    return columns_[idx];
}

int32_t Schema::GetColIdx(const std::string& name) const {
    for (uint32_t i = 0; i < columns_.size(); i++) {
        if (columns_[i].column_name == name) return static_cast<int32_t>(i);
    }
    return -1;
}

bool Schema::HasColumn(const std::string& name) const {
    return GetColIdx(name) >= 0;
}

uint32_t Schema::GetFixedLengthSize() const {
    uint32_t total = 0;
    for (const auto& col : columns_) {
        total += col.GetFixedSize();
    }
    return total;
}

std::string Schema::ToString() const {
    std::ostringstream oss;
    oss << "Schema(";
    for (size_t i = 0; i < columns_.size(); i++) {
        if (i > 0) oss << ", ";
        oss << columns_[i].ToString();
    }
    oss << ")";
    return oss.str();
}

std::string Schema::Serialize() const {
    // Format: [col_count:4B][for each col: name_len:2B + name + type:1B + max_len:4B + nullable:1B]
    std::string data;
    uint32_t col_count = columns_.size();
    data.append(reinterpret_cast<const char*>(&col_count), 4);

    for (const auto& col : columns_) {
        uint16_t name_len = col.column_name.size();
        data.append(reinterpret_cast<const char*>(&name_len), 2);
        data.append(col.column_name);
        data.push_back(static_cast<char>(col.column_type));
        uint32_t max_len = col.max_length;
        data.append(reinterpret_cast<const char*>(&max_len), 4);
        data.push_back(col.is_nullable ? 1 : 0);
    }
    return data;
}

Schema Schema::Deserialize(const char* data, uint32_t len) {
    if (len < 4) return Schema();

    uint32_t col_count;
    std::memcpy(&col_count, data, 4);
    const char* ptr = data + 4;

    std::vector<Column> columns;
    columns.reserve(col_count);

    for (uint32_t i = 0; i < col_count; i++) {
        uint16_t name_len;
        std::memcpy(&name_len, ptr, 2);
        ptr += 2;

        std::string name(ptr, name_len);
        ptr += name_len;

        TypeId type = static_cast<TypeId>(*ptr);
        ptr += 1;

        uint32_t max_len;
        std::memcpy(&max_len, ptr, 4);
        ptr += 4;

        bool nullable = (*ptr != 0);
        ptr += 1;

        columns.emplace_back(name, type, max_len, nullable);
    }

    return Schema(std::move(columns));
}

}  // namespace goods_db
