#include "storage/table/tuple.h"
#include <cstring>
#include <sstream>
#include "common/logger.h"

namespace goods_db {

// Static helper for deserializing a fixed-length value
static Value DeserializeValue(const char* data, TypeId type) {
    switch (type) {
        case TypeId::BOOLEAN: return Value::CreateBoolean(data[0] != 0);
        case TypeId::TINYINT: {
            int8_t v; std::memcpy(&v, data, 1); return Value::CreateTinyInt(v);
        }
        case TypeId::SMALLINT: {
            int16_t v; std::memcpy(&v, data, 2); return Value::CreateSmallInt(v);
        }
        case TypeId::INTEGER: {
            int32_t v; std::memcpy(&v, data, 4); return Value::CreateInteger(v);
        }
        case TypeId::BIGINT: {
            int64_t v; std::memcpy(&v, data, 8); return Value::CreateBigInt(v);
        }
        case TypeId::DECIMAL: {
            double v; std::memcpy(&v, data, 8); return Value::CreateDecimal(v);
        }
        case TypeId::TIMESTAMP: {
            int64_t v; std::memcpy(&v, data, 8); return Value::CreateTimestamp(v);
        }
        default: return Value();
    }
}

Tuple Tuple::CreateFromValues(const std::vector<Value>& values,
                               const Schema* schema) {
    Tuple tuple;
    if (!schema || values.empty()) return tuple;

    uint32_t col_count = schema->GetColumnCount();
    GOODS_DB_ASSERT(values.size() == col_count, "Value count mismatch");

    // Layout:
    // [col_count:2B][total_size:2B][null_bitmap:N B][fixed_col_data...]
    // [var_col_offsets: 4B each][var_col_data: [len:4B][data]...]

    uint32_t header_size = 4;  // col_count(2B) + total_length(2B)
    uint32_t null_bitmap_size = (col_count + 7) / 8;

    // Calculate total size of fixed-length columns only (non-VARCHAR)
    uint32_t fixed_data_size = 0;
    uint32_t varchar_count = 0;
    for (uint32_t i = 0; i < col_count; i++) {
        const auto& col = schema->GetColumn(i);
        if (col.column_type == TypeId::VARCHAR) {
            varchar_count++;
        } else {
            fixed_data_size += col.GetFixedSize();
        }
    }

    uint32_t var_offset_area_size = varchar_count * 4;  // 4 bytes per VARCHAR offset
    uint32_t var_data_start = header_size + null_bitmap_size +
                              fixed_data_size + var_offset_area_size;

    // Calculate total size and VARCHAR offsets
    uint32_t total_size = var_data_start;
    std::vector<uint32_t> varchar_offsets;
    for (uint32_t i = 0; i < col_count; i++) {
        const auto& col = schema->GetColumn(i);
        if (col.column_type == TypeId::VARCHAR) {
            varchar_offsets.push_back(total_size);
            total_size += 4 + values[i].GetAsVarchar().size();  // 4B length + data
        }
    }

    // Allocate and zero-initialize
    tuple.data_.resize(total_size, 0);

    // Write header
    uint16_t raw_col_count = static_cast<uint16_t>(col_count);
    uint16_t raw_total = static_cast<uint16_t>(total_size);
    std::memcpy(tuple.data_.data(), &raw_col_count, 2);
    std::memcpy(tuple.data_.data() + 2, &raw_total, 2);

    // Null bitmap (all zeros = no nulls)
    std::memset(tuple.data_.data() + header_size, 0, null_bitmap_size);

    // Write fixed-length column data and VARCHAR offsets
    char* fixed_ptr = tuple.data_.data() + header_size + null_bitmap_size;
    char* var_offset_ptr = tuple.data_.data() + header_size + null_bitmap_size +
                           fixed_data_size;

    size_t varchar_idx = 0;
    for (uint32_t i = 0; i < col_count; i++) {
        const auto& col = schema->GetColumn(i);
        if (col.column_type == TypeId::VARCHAR) {
            // Write the offset where this VARCHAR's data starts
            uint32_t offset = varchar_offsets[varchar_idx];
            std::memcpy(var_offset_ptr, &offset, 4);
            var_offset_ptr += 4;

            // Write VARCHAR data with 4-byte length prefix
            const std::string& str = values[i].GetAsVarchar();
            uint32_t str_len = str.size();
            std::memcpy(tuple.data_.data() + offset, &str_len, 4);
            std::memcpy(tuple.data_.data() + offset + 4, str.data(), str_len);
            varchar_idx++;
        } else {
            // Write fixed-length data
            uint32_t fixed_size = col.GetFixedSize();
            std::string serialized(fixed_size, '\0');
            values[i].SerializeTo(serialized.data());
            std::memcpy(fixed_ptr, serialized.data(), fixed_size);
            fixed_ptr += fixed_size;
        }
    }

    return tuple;
}

Tuple Tuple::Deserialize(const char* data, const Schema* schema) {
    Tuple tuple;
    uint16_t total_size;
    std::memcpy(&total_size, data + 2, 2);
    tuple.data_.assign(data, total_size);
    return tuple;
}

const char* Tuple::Serialize(const Schema* schema) {
    // The tuple is already serialized in data_
    return data_.data();
}

Value Tuple::GetValue(const Schema* schema, uint32_t col_idx) const {
    if (!schema || col_idx >= schema->GetColumnCount()) {
        return Value();
    }

    const Column& col = schema->GetColumn(col_idx);
    uint32_t header_size = 4;
    uint32_t null_bitmap_size = (schema->GetColumnCount() + 7) / 8;
    uint32_t fixed_offset = header_size + null_bitmap_size;

    // Calculate where var offset area starts (after all fixed-length columns)
    uint32_t fixed_data_size = 0;
    for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
        const auto& c = schema->GetColumn(i);
        if (c.column_type != TypeId::VARCHAR) {
            fixed_data_size += c.GetFixedSize();
        }
    }
    uint32_t var_offset_base = fixed_offset + fixed_data_size;

    if (col.column_type == TypeId::VARCHAR) {
        // Find which VARCHAR index this column is
        int varchar_idx = -1;
        for (uint32_t i = 0; i <= col_idx; i++) {
            if (schema->GetColumn(i).column_type == TypeId::VARCHAR) varchar_idx++;
        }

        // Read offset from var_offset area
        uint32_t var_data_offset;
        std::memcpy(&var_data_offset,
                    data_.data() + var_offset_base + varchar_idx * 4, 4);

        // Read VARCHAR: first 4 bytes = length, rest = data
        uint32_t str_len;
        std::memcpy(&str_len, data_.data() + var_data_offset, 4);

        std::string str(data_.data() + var_data_offset + 4, str_len);
        return Value::CreateVarchar(str);
    }

    // Fixed-length column: calculate its position within the fixed data area
    uint32_t col_offset = fixed_offset;
    for (uint32_t i = 0; i < col_idx; i++) {
        col_offset += schema->GetColumn(i).GetFixedSize();
    }

    return DeserializeValue(data_.data() + col_offset, col.column_type);
}

bool Tuple::Equals(const Tuple& other, const Schema* schema) const {
    if (data_.size() != other.data_.size()) return false;
    return std::memcmp(data_.data(), other.data_.data(), data_.size()) == 0;
}

std::string Tuple::ToString(const Schema* schema) const {
    std::ostringstream oss;
    oss << "Tuple(";
    for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
        if (i > 0) oss << ", ";
        oss << GetValue(schema, i).ToString();
    }
    oss << ")";
    return oss.str();
}

}  // namespace goods_db
