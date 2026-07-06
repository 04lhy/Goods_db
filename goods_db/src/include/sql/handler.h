#pragma once

#include <cstdint>
#include <string>
#include "common/rid.h"
#include "type/schema.h"
#include "type/value.h"

namespace goods_db {

class Tuple;

/**
 * handler - abstract base class for table access.
 *
 * This is the core abstraction of the storage engine interface layer.
 * Based on MySQL's handler class design.
 *
 * Each table has one handler instance. The handler delegates operations
 * to the underlying storage engine components (TableHeap, Index, etc.).
 */
class handler {
public:
    handler() = default;
    virtual ~handler() = default;

    // =========================================================================
    // Table Lifecycle (DDL)
    // =========================================================================

    /** Create a new table */
    virtual int create(const char* name, Schema* schema) = 0;

    /** Open an existing table for access */
    virtual int open(const char* name) = 0;

    /** Close the table */
    virtual int close() = 0;

    /** Delete the table */
    virtual int delete_table(const char* name) = 0;

    // =========================================================================
    // Full Table Scan
    // =========================================================================

    /** Initialize a full table scan */
    virtual int rnd_init(bool scan_all) = 0;

    /** Get the next row in the scan */
    virtual int rnd_next(Tuple* buf) = 0;

    /** Get a row by RID (random access) */
    virtual int rnd_pos(Tuple* buf, RID rid) = 0;

    /** End the full table scan */
    virtual int rnd_end() = 0;

    // =========================================================================
    // Index Scan
    // =========================================================================

    /** Initialize index scan on a specific index */
    virtual int index_init(uint32_t idx_id, bool sorted) = 0;

    /** Index lookup by key */
    virtual int index_read(Tuple* buf, const Value* key) = 0;

    /** Get next row in index order */
    virtual int index_next(Tuple* buf) = 0;

    /** End index scan */
    virtual int index_end() = 0;

    // =========================================================================
    // Row-level DML
    // =========================================================================

    /** Insert a row */
    virtual int write_row(const Tuple& tuple) = 0;

    /** Update a row */
    virtual int update_row(const RID& rid, const Tuple& new_tuple) = 0;

    /** Delete a row */
    virtual int delete_row(const RID& rid) = 0;

    // =========================================================================
    // Information
    // =========================================================================

    /** Return total row count (may be approximate) */
    virtual uint64_t records() = 0;

    /** Return table capability flags */
    virtual uint64_t table_flags() const = 0;

    /** Get the table name */
    virtual const char* get_table_name() const = 0;

    /** Get the schema */
    virtual const Schema* get_schema() const = 0;
};

}  // namespace goods_db
