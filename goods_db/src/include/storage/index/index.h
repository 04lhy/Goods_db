#pragma once

#include <cstdint>
#include <vector>
#include "common/rid.h"

namespace goods_db {

/**
 * Abstract Index interface.
 * All index implementations (B+Tree, Hash) implement this.
 */
class Index {
public:
    Index() = default;
    virtual ~Index() = default;

    /** Insert a key-rid pair into the index */
    virtual bool Insert(int64_t key, const RID& rid) = 0;

    /** Remove a key from the index */
    virtual bool Remove(int64_t key) = 0;

    /** Lookup a single key. Returns the RID or invalid RID if not found. */
    virtual RID GetValue(int64_t key) = 0;

    /** Range scan. Returns all (key, rid) pairs where start <= key <= end. */
    virtual std::vector<std::pair<int64_t, RID>> RangeScan(int64_t start_key,
                                                             int64_t end_key) = 0;

    /** Get total number of entries in the index */
    virtual size_t GetSize() const = 0;

    /** Get the index type name */
    virtual const char* GetName() const = 0;
};

}  // namespace goods_db
