#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <mutex>
#include "type/schema.h"

namespace goods_db {

// Forward declarations
class handler;

/**
 * handlerton - storage engine singleton descriptor.
 *
 * Each storage engine provides one handlerton structure.
 * The handlerton contains callbacks for engine lifecycle, handler creation,
 * and transaction support.
 *
 * Based on MySQL's handlerton design.
 */
struct handlerton {
    /** Engine name (e.g., "goods_engine") */
    const char* name;

    /** Engine initialization callback. Called at server startup. */
    int (*init)();

    /** Engine deinitialization callback. Called at server shutdown. */
    int (*deinit)();

    /** Factory function to create a handler for a table */
    class handler* (*create_handler)(const char* table_name, Schema* schema);

    /** Transaction commit callback (reserved for future use) */
    int (*commit)(void* trx);

    /** Transaction rollback callback (reserved for future use) */
    int (*rollback)(void* trx);

    /** Engine capability flags */
    uint64_t flags;
};

// Engine capability flags
constexpr uint64_t HTON_FLAG_SUPPORTS_INDEXES   = 0x0001;
constexpr uint64_t HTON_FLAG_SUPPORTS_TRANSACTIONS = 0x0002;
constexpr uint64_t HTON_FLAG_SUPPORTS_BACKUP    = 0x0004;

// =============================================================================
// Global Engine Registry
// =============================================================================

/**
 * Register a storage engine.
 * Called at server startup by each engine's init function.
 * Thread-safe.
 * @return true on success, false if engine name already registered
 */
bool register_engine(handlerton* engine);

/**
 * Get a registered engine by name.
 * @return handlerton pointer, or nullptr if not found
 */
handlerton* get_engine(const std::string& name);

/**
 * Get all registered engines.
 */
std::vector<handlerton*> get_all_engines();

/**
 * Unregister an engine.
 * @return true on success
 */
bool unregister_engine(const std::string& name);

}  // namespace goods_db
