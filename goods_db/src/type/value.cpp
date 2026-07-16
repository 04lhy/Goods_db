#include "type/value.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "common/logger.h"

namespace goods_db {

// Factory methods
Value Value::CreateBoolean(bool val) {
    Value v;
    v.type_id_ = TypeId::BOOLEAN;
    v.data_.boolean = val;
    return v;
}

Value Value::CreateTinyInt(int8_t val) {
    Value v;
    v.type_id_ = TypeId::TINYINT;
    v.data_.tinyint = val;
    return v;
}

Value Value::CreateSmallInt(int16_t val) {
    Value v;
    v.type_id_ = TypeId::SMALLINT;
    v.data_.smallint = val;
    return v;
}

Value Value::CreateInteger(int32_t val) {
    Value v;
    v.type_id_ = TypeId::INTEGER;
    v.data_.integer = val;
    return v;
}

Value Value::CreateBigInt(int64_t val) {
    Value v;
    v.type_id_ = TypeId::BIGINT;
    v.data_.bigint = val;
    return v;
}

Value Value::CreateDecimal(double val) {
    Value v;
    v.type_id_ = TypeId::DECIMAL;
    v.data_.decimal = val;
    return v;
}

Value Value::CreateTimestamp(int64_t val) {
    Value v;
    v.type_id_ = TypeId::TIMESTAMP;
    v.data_.timestamp = val;
    return v;
}

int64_t Value::ParseTimestamp(const std::string& str) {
    // Parse "YYYY-MM-DD HH:MM:SS" or "YYYY-MM-DD HH:MM:SS.xxx"
    // Returns seconds since epoch (1970-01-01 00:00:00)
    if (str.empty()) return 0;
    int year = 0, month = 1, day = 1, hour = 0, minute = 0, second = 0;
    sscanf(str.c_str(), "%d-%d-%d %d:%d:%d",
           &year, &month, &day, &hour, &minute, &second);
    if (year < 1970) return 0;
    // Simple epoch conversion (valid for years 1970-2099)
    static const int days_before_month[] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    auto is_leap = [](int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); };
    int64_t days = 0;
    for (int y = 1970; y < year; y++) days += is_leap(y) ? 366 : 365;
    days += days_before_month[month - 1];
    if (month > 2 && is_leap(year)) days++;
    days += (day - 1);
    return days * 86400 + hour * 3600 + minute * 60 + second;
}

std::string Value::FormatTimestamp(int64_t epoch) {
    if (epoch <= 0) return "1970-01-01 00:00:00";
    static const int days_before_month[] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    auto is_leap = [](int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); };

    int64_t remaining = epoch;
    int second = remaining % 60; remaining /= 60;
    int minute = remaining % 60; remaining /= 60;
    int hour   = remaining % 24; remaining /= 24;
    int64_t days = remaining;

    int year = 1970;
    while (true) {
        int days_in_year = is_leap(year) ? 366 : 365;
        if (days < days_in_year) break;
        days -= days_in_year;
        year++;
    }

    int month = 1;
    while (month <= 12) {
        int days_in_month = days_before_month[month - 1];
        if (month == 2 && is_leap(year)) days_in_month = 60;  // Feb in leap year
        int next_month_days = (month < 12) ? days_before_month[month] : 0;
        if (month == 1 && is_leap(year)) next_month_days = 60;
        if (month == 2 && is_leap(year)) {
            next_month_days = 62;  // March starts at 61 in leap year
        }
        int dim = (month < 12) ? (days_before_month[month] +
            (month == 2 && is_leap(year) ? 1 : 0) +
            (month == 1 && is_leap(year) ? 0 : 0)) : 0;
        // Simpler: just compute month length
        int mlen;
        if (month == 2) mlen = is_leap(year) ? 29 : 28;
        else if (month == 4 || month == 6 || month == 9 || month == 11) mlen = 30;
        else mlen = 31;
        if (days < mlen) break;
        days -= mlen;
        month++;
    }

    int day = static_cast<int>(days) + 1;

    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             year, month, day, hour, minute, second);
    return std::string(buf);
}

Value Value::CreateVarchar(const std::string& val) {
    Value v;
    v.type_id_ = TypeId::VARCHAR;
    v.varchar_data_ = val;
    return v;
}

// Accessors
bool Value::GetAsBoolean() const {
    GOODS_DB_ASSERT(type_id_ == TypeId::BOOLEAN, "Type mismatch");
    return data_.boolean;
}
int8_t Value::GetAsTinyInt() const {
    GOODS_DB_ASSERT(type_id_ == TypeId::TINYINT, "Type mismatch");
    return data_.tinyint;
}
int16_t Value::GetAsSmallInt() const {
    GOODS_DB_ASSERT(type_id_ == TypeId::SMALLINT, "Type mismatch");
    return data_.smallint;
}
int32_t Value::GetAsInteger() const {
    GOODS_DB_ASSERT(type_id_ == TypeId::INTEGER, "Type mismatch");
    return data_.integer;
}
int64_t Value::GetAsBigInt() const {
    GOODS_DB_ASSERT(type_id_ == TypeId::BIGINT, "Type mismatch");
    return data_.bigint;
}
double Value::GetAsDecimal() const {
    GOODS_DB_ASSERT(type_id_ == TypeId::DECIMAL, "Type mismatch");
    return data_.decimal;
}
int64_t Value::GetAsTimestamp() const {
    GOODS_DB_ASSERT(type_id_ == TypeId::TIMESTAMP, "Type mismatch");
    return data_.timestamp;
}
const std::string& Value::GetAsVarchar() const {
    GOODS_DB_ASSERT(type_id_ == TypeId::VARCHAR, "Type mismatch");
    return varchar_data_;
}

// =============================================================================
// Numeric type promotion
// =============================================================================
namespace {

// Promotion rank for numeric types (higher = wider)
int NumericRank(TypeId type) {
    switch (type) {
        case TypeId::TINYINT:  return 1;
        case TypeId::SMALLINT: return 2;
        case TypeId::INTEGER:  return 3;
        case TypeId::BIGINT:   return 4;
        case TypeId::DECIMAL:  return 5;
        default: return 0;
    }
}

bool IsNumericType(TypeId type) {
    return type == TypeId::TINYINT || type == TypeId::SMALLINT ||
           type == TypeId::INTEGER || type == TypeId::BIGINT ||
           type == TypeId::DECIMAL;
}

// Convert a numeric Value to double for comparison
double ToDouble(const Value& v) {
    switch (v.GetTypeId()) {
        case TypeId::TINYINT:  return static_cast<double>(v.GetAsTinyInt());
        case TypeId::SMALLINT: return static_cast<double>(v.GetAsSmallInt());
        case TypeId::INTEGER:  return static_cast<double>(v.GetAsInteger());
        case TypeId::BIGINT:   return static_cast<double>(v.GetAsBigInt());
        case TypeId::DECIMAL:  return v.GetAsDecimal();
        default: return 0.0;
    }
}

}  // namespace

// Comparison
bool Value::operator==(const Value& other) const {
    // Cross-type numeric comparison: promote both to double
    if (IsNumericType(type_id_) && IsNumericType(other.type_id_)) {
        return ToDouble(*this) == ToDouble(other);
    }

    if (type_id_ != other.type_id_) return false;
    switch (type_id_) {
        case TypeId::INVALID: return true;
        case TypeId::BOOLEAN: return data_.boolean == other.data_.boolean;
        case TypeId::TINYINT: return data_.tinyint == other.data_.tinyint;
        case TypeId::SMALLINT: return data_.smallint == other.data_.smallint;
        case TypeId::INTEGER: return data_.integer == other.data_.integer;
        case TypeId::BIGINT: return data_.bigint == other.data_.bigint;
        case TypeId::DECIMAL: return data_.decimal == other.data_.decimal;
        case TypeId::TIMESTAMP: return data_.timestamp == other.data_.timestamp;
        case TypeId::VARCHAR: return varchar_data_ == other.varchar_data_;
    }
    return false;
}

bool Value::operator<(const Value& other) const {
    // Cross-type numeric comparison: promote both to double
    if (IsNumericType(type_id_) && IsNumericType(other.type_id_)) {
        return ToDouble(*this) < ToDouble(other);
    }

    GOODS_DB_ASSERT(type_id_ == other.type_id_,
                    "Type mismatch in comparison");
    switch (type_id_) {
        case TypeId::BOOLEAN: return static_cast<int>(data_.boolean) < static_cast<int>(other.data_.boolean);
        case TypeId::TINYINT: return data_.tinyint < other.data_.tinyint;
        case TypeId::SMALLINT: return data_.smallint < other.data_.smallint;
        case TypeId::INTEGER: return data_.integer < other.data_.integer;
        case TypeId::BIGINT: return data_.bigint < other.data_.bigint;
        case TypeId::DECIMAL: return data_.decimal < other.data_.decimal;
        case TypeId::TIMESTAMP: return data_.timestamp < other.data_.timestamp;
        case TypeId::VARCHAR: return varchar_data_ < other.varchar_data_;
        default: return false;
    }
}

std::string Value::ToString() const {
    switch (type_id_) {
        case TypeId::INVALID: return "NULL";
        case TypeId::BOOLEAN: return data_.boolean ? "true" : "false";
        case TypeId::TINYINT: return std::to_string(data_.tinyint);
        case TypeId::SMALLINT: return std::to_string(data_.smallint);
        case TypeId::INTEGER: return std::to_string(data_.integer);
        case TypeId::BIGINT: return std::to_string(data_.bigint);
        case TypeId::DECIMAL: return std::to_string(data_.decimal);
        case TypeId::TIMESTAMP: return "'" + FormatTimestamp(data_.timestamp) + "'";
        case TypeId::VARCHAR: return "'" + varchar_data_ + "'";
    }
    return "?";
}

uint32_t Value::GetSerializedSize() const {
    switch (type_id_) {
        case TypeId::BOOLEAN: return 1;
        case TypeId::TINYINT: return 1;
        case TypeId::SMALLINT: return 2;
        case TypeId::INTEGER: return 4;
        case TypeId::BIGINT: return 8;
        case TypeId::DECIMAL: return 8;
        case TypeId::TIMESTAMP: return 8;
        case TypeId::VARCHAR: return 4 + varchar_data_.size();  // 4B length + data
        default: return 0;
    }
}

uint32_t Value::SerializeTo(char* buffer) const {
    switch (type_id_) {
        case TypeId::BOOLEAN:
            buffer[0] = data_.boolean ? 1 : 0;
            return 1;
        case TypeId::TINYINT:
            buffer[0] = data_.tinyint;
            return 1;
        case TypeId::SMALLINT:
            std::memcpy(buffer, &data_.smallint, 2);
            return 2;
        case TypeId::INTEGER:
            std::memcpy(buffer, &data_.integer, 4);
            return 4;
        case TypeId::BIGINT:
            std::memcpy(buffer, &data_.bigint, 8);
            return 8;
        case TypeId::DECIMAL:
            std::memcpy(buffer, &data_.decimal, 8);
            return 8;
        case TypeId::TIMESTAMP:
            std::memcpy(buffer, &data_.timestamp, 8);
            return 8;
        case TypeId::VARCHAR: {
            uint32_t len = varchar_data_.size();
            std::memcpy(buffer, &len, 4);
            std::memcpy(buffer + 4, varchar_data_.data(), len);
            return 4 + len;
        }
        default: return 0;
    }
}

uint32_t Value::DeserializeFrom(const char* buffer, TypeId type) {
    type_id_ = type;
    switch (type) {
        case TypeId::BOOLEAN:
            data_.boolean = (buffer[0] != 0);
            return 1;
        case TypeId::TINYINT:
            data_.tinyint = buffer[0];
            return 1;
        case TypeId::SMALLINT:
            std::memcpy(&data_.smallint, buffer, 2);
            return 2;
        case TypeId::INTEGER:
            std::memcpy(&data_.integer, buffer, 4);
            return 4;
        case TypeId::BIGINT:
            std::memcpy(&data_.bigint, buffer, 8);
            return 8;
        case TypeId::DECIMAL:
            std::memcpy(&data_.decimal, buffer, 8);
            return 8;
        case TypeId::TIMESTAMP:
            std::memcpy(&data_.timestamp, buffer, 8);
            return 8;
        case TypeId::VARCHAR: {
            uint32_t len;
            std::memcpy(&len, buffer, 4);
            varchar_data_.assign(buffer + 4, len);
            return 4 + len;
        }
        default: return 0;
    }
}

}  // namespace goods_db
