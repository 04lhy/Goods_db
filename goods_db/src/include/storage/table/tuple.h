#pragma once

#include <string>
#include <vector>
#include "common/rid.h"
#include "type/schema.h"
#include "type/value.h"

namespace goods_db {

/**
 * Tuple - a row of data in a table.
 *
 * Serialization format:
 * ┌────────────────────────────────────┐
 * │ Header: 4 bytes                    │
 * │   - col_count (2 bytes)            │
 * │   - total_length (2 bytes)         │
 * ├────────────────────────────────────┤
 * │ Null bitmap (ceil(col_count/8))    │
 * ├────────────────────────────────────┤
 * │ Fixed-length column data           │
 * │   col0 | col1 | col2 | ...         │
 * ├────────────────────────────────────┤
 * │ Variable-length column offsets     │
 * │   vcol_offset[0] | vcol_offset[1]  │
 * ├────────────────────────────────────┤
 * │ Variable-length column data        │
 * │   vcol0 data | vcol1 data | ...    │
 * └────────────────────────────────────┘
 */
class Tuple {
public:
    Tuple() = default;

    /** Create a tuple from a vector of Values */
    static Tuple CreateFromValues(const std::vector<Value>& values,
                                   const Schema* schema);

    /** Deserialize a tuple from raw bytes */
    static Tuple Deserialize(const char* data, const Schema* schema);

    /** Serialize tuple to byte array. Returns pointer to internal data. */
    const char* Serialize(const Schema* schema);

    /** Get serialized size */
    uint32_t GetLength() const { return data_.size(); }

    /** Get raw serialized data */
    const char* GetData() const { return data_.data(); }

    /** Get a Value by column index */
    Value GetValue(const Schema* schema, uint32_t col_idx) const;

    /** Get the RID of this tuple (set after insertion) */
    RID GetRid() const { return rid_; }
    void SetRid(const RID& rid) { rid_ = rid; }

    /** Compare two tuples for equality (all columns) */
    bool Equals(const Tuple& other, const Schema* schema) const;

    /** Get human-readable representation */
    std::string ToString(const Schema* schema) const;

private:
    RID rid_;
    std::string data_;  // serialized tuple data
};

}  // namespace goods_db
