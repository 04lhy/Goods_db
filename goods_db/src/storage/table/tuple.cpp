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
            uint32_t str_len = (values[i].GetTypeId() != TypeId::INVALID)
                                   ? values[i].GetAsVarchar().size()
                                   : 0;
            total_size += 4 + str_len;  // 4B length + data
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

    // Set null bitmap bits for NULL (TypeId::INVALID) values
    for (uint32_t i = 0; i < col_count; i++) {
        if (values[i].GetTypeId() == TypeId::INVALID) {
            uint8_t byte_idx = i / 8;
            uint8_t bit_idx = i % 8;
            tuple.data_[header_size + byte_idx] |= static_cast<char>(1 << bit_idx);
        }
    }

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
            // Handle unspecified columns (TypeId::INVALID) by defaulting to empty string
            uint32_t str_len = 0;
            std::string empty_str;
            const std::string* str_ptr = &empty_str;
            if (values[i].GetTypeId() != TypeId::INVALID) {
                str_ptr = &values[i].GetAsVarchar();
                str_len = str_ptr->size();
            }
            std::memcpy(tuple.data_.data() + offset, &str_len, 4);
            if (str_len > 0) {
                std::memcpy(tuple.data_.data() + offset + 4, str_ptr->data(), str_len);
            }
            varchar_idx++;
        } else {
            // Write fixed-length data
            // Handle unspecified columns (TypeId::INVALID) by defaulting to zero.
            // Also coerce VARCHAR values (from parser string literals) to the
            // column's actual type to avoid buffer overflow (VARCHAR serialization
            // writes 4+len bytes, but fixed-size columns have only GetFixedSize()).
            uint32_t fixed_size = col.GetFixedSize();
            std::string serialized(fixed_size, '\0');
            TypeId val_type = values[i].GetTypeId();
            if (val_type != TypeId::INVALID) {
                if (val_type == col.column_type) {
                    values[i].SerializeTo(serialized.data());
                } else {
                    // Coerce between types (parser produces BIGINT for all integers)
                    bool is_val_numeric = (val_type >= TypeId::TINYINT && val_type <= TypeId::DECIMAL);
                    bool is_col_numeric = (col.column_type >= TypeId::TINYINT && col.column_type <= TypeId::DECIMAL);
                    if (is_val_numeric && is_col_numeric) {
                        int64_t num = 0;
                        switch (val_type) {
                            case TypeId::TINYINT:  num = values[i].GetAsTinyInt(); break;
                            case TypeId::SMALLINT: num = values[i].GetAsSmallInt(); break;
                            case TypeId::INTEGER:  num = values[i].GetAsInteger(); break;
                            case TypeId::BIGINT:   num = values[i].GetAsBigInt(); break;
                            case TypeId::DECIMAL:  num = static_cast<int64_t>(values[i].GetAsDecimal()); break;
                            default: break;
                        }
                        switch (col.column_type) {
                            case TypeId::TINYINT:  { auto v = static_cast<int8_t>(num);  std::memcpy(serialized.data(), &v, 1); break; }
                            case TypeId::SMALLINT: { auto v = static_cast<int16_t>(num); std::memcpy(serialized.data(), &v, 2); break; }
                            case TypeId::INTEGER:  { auto v = static_cast<int32_t>(num); std::memcpy(serialized.data(), &v, 4); break; }
                            case TypeId::BIGINT:   { std::memcpy(serialized.data(), &num, 8); break; }
                            case TypeId::DECIMAL:  { auto v = static_cast<double>(num); std::memcpy(serialized.data(), &v, 8); break; }
                            case TypeId::BOOLEAN:  { serialized[0] = (num != 0) ? 1 : 0; break; }
                            case TypeId::TIMESTAMP:{ std::memcpy(serialized.data(), &num, 8); break; }
                            default: break;
                        }
                    } else if (val_type == TypeId::VARCHAR) {
                    // Coerce string literal → target type
                    const std::string& str = values[i].GetAsVarchar();
                    switch (col.column_type) {
                        case TypeId::BOOLEAN:
                            serialized[0] = (str == "true" || str == "1") ? 1 : 0;
                            break;
                        case TypeId::TINYINT: {
                            int8_t v = static_cast<int8_t>(std::stoll(str));
                            serialized[0] = v;
                            break;
                        }
                        case TypeId::SMALLINT: {
                            int16_t v = static_cast<int16_t>(std::stoll(str));
                            std::memcpy(serialized.data(), &v, 2);
                            break;
                        }
                        case TypeId::INTEGER: {
                            int32_t v = static_cast<int32_t>(std::stoll(str));
                            std::memcpy(serialized.data(), &v, 4);
                            break;
                        }
                        case TypeId::BIGINT: {
                            int64_t v = std::stoll(str);
                            std::memcpy(serialized.data(), &v, 8);
                            break;
                        }
                        case TypeId::DECIMAL: {
                            double v = std::stod(str);
                            std::memcpy(serialized.data(), &v, 8);
                            break;
                        }
                        case TypeId::TIMESTAMP: {
                            int64_t v = Value::ParseTimestamp(str);
                            std::memcpy(serialized.data(), &v, 8);
                            break;
                        }
                        default: break;
                    }
                    }
                }
                // else: other type mismatches → zero (already set)
            }
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

    // Check null bitmap — return TypeId::INVALID (NULL) if bit is set
    {
        uint8_t byte_idx = col_idx / 8;
        uint8_t bit_idx = col_idx % 8;
        if (static_cast<uint8_t>(data_[header_size + byte_idx]) & (1 << bit_idx)) {
            return Value();  // NULL → TypeId::INVALID
        }
    }

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
