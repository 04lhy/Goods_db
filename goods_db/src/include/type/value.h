#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include "type/schema.h"

namespace goods_db {

/**
 * Value - a tagged union for all supported data types.
 *
 * Layout:
 *   - Fixed-length types store directly in union
 *   - VARCHAR stores up to 255 chars inline (for simplicity), or a pointer
 *      to heap-allocated string for longer values
 */
class Value {
public:
    Value() : type_id_(TypeId::INVALID) { std::memset(&data_, 0, sizeof(data_)); }

    // Factory methods
    static Value CreateBoolean(bool val);
    static Value CreateTinyInt(int8_t val);
    static Value CreateSmallInt(int16_t val);
    static Value CreateInteger(int32_t val);
    static Value CreateBigInt(int64_t val);
    static Value CreateDecimal(double val);
    static Value CreateTimestamp(int64_t val);
    static Value CreateVarchar(const std::string& val);

    /** Parse "YYYY-MM-DD HH:MM:SS" string to int64_t epoch seconds */
    static int64_t ParseTimestamp(const std::string& str);

    /** Format int64_t epoch seconds to "YYYY-MM-DD HH:MM:SS" string */
    static std::string FormatTimestamp(int64_t epoch);

    // Type accessor
    TypeId GetTypeId() const { return type_id_; }

    // Value accessors (type-checked via assert)
    bool GetAsBoolean() const;
    int8_t GetAsTinyInt() const;
    int16_t GetAsSmallInt() const;
    int32_t GetAsInteger() const;
    int64_t GetAsBigInt() const;
    double GetAsDecimal() const;
    int64_t GetAsTimestamp() const;
    const std::string& GetAsVarchar() const;

    // Comparison operators
    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const { return !(*this == other); }
    bool operator<(const Value& other) const;
    bool operator>(const Value& other) const { return other < *this; }
    bool operator<=(const Value& other) const { return !(other < *this); }
    bool operator>=(const Value& other) const { return !(*this < other); }

    /** Convert to human-readable string */
    std::string ToString() const;

    /** Serialize value to byte buffer. Returns number of bytes written. */
    uint32_t SerializeTo(char* buffer) const;

    /** Deserialize value from byte buffer. Returns number of bytes read. */
    uint32_t DeserializeFrom(const char* buffer, TypeId type);

    /** Get serialized size of this value */
    uint32_t GetSerializedSize() const;

private:
    TypeId type_id_;
    std::string varchar_data_;  // storage for VARCHAR values

    union Data {
        bool boolean;
        int8_t tinyint;
        int16_t smallint;
        int32_t integer;
        int64_t bigint;
        double decimal;
        int64_t timestamp;
    } data_;
};

}  // namespace goods_db
