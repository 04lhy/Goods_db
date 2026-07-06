#pragma once

#include <cstddef>
#include <cstdint>

namespace goods_db {

// =============================================================================
// Type Aliases
// =============================================================================

/// Page identifier (file_id in upper 16 bits, page_no in lower 16 bits)
using page_id_t = int32_t;

/// Buffer pool frame identifier
using frame_id_t = int32_t;

// =============================================================================
// Compile-time Constants
// =============================================================================

/// Page size: 16 KB
constexpr size_t PAGE_SIZE = 16384;

/// Default buffer pool size: 64 MB
constexpr size_t BUFFER_POOL_DEFAULT_SIZE = 64 * 1024 * 1024;

/// Maximum tuple size: 8 KB (half a page)
constexpr size_t MAX_TUPLE_SIZE = 8192;

/// Default number of buffer pool frames
constexpr size_t BUFFER_POOL_DEFAULT_FRAMES = BUFFER_POOL_DEFAULT_SIZE / PAGE_SIZE;

/// Invalid page ID
constexpr int32_t INVALID_PAGE_ID = -1;

/// Invalid frame ID
constexpr int32_t INVALID_FRAME_ID = -1;

/// Invalid transaction ID
constexpr int32_t INVALID_TXN_ID = -1;

/// Database version string
constexpr const char* GOODS_DB_VERSION = "0.1.0";

/// Default data directory
constexpr const char* DEFAULT_DATA_DIR = "./data";

/// Default port
constexpr int32_t DEFAULT_PORT = 3307;

/// B+Tree branching factor
constexpr int32_t BPLUSTREE_MAX_KEYS = 256;

}  // namespace goods_db
