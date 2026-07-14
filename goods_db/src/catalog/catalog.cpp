#include "catalog/catalog.h"
#include "common/logger.h"

#include <algorithm>
#include <cctype>

namespace goods_db {

Catalog::Catalog(BufferPoolManager* bpm) : bpm_(bpm), default_db_(DEFAULT_DATABASE) {
    // Ensure the default database exists on construction
    GetOrCreateDefaultDB();
}

// =============================================================================
// Database Operations
// =============================================================================

std::string Catalog::NormalizeName(const std::string& name) {
    std::string result = name;
    // Convert to lowercase for case-insensitive comparison
    for (auto& c : result) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

DatabaseInfo* Catalog::GetOrCreateDefaultDB() {
    std::string key = NormalizeName(default_db_);
    auto it = databases_.find(key);
    if (it == databases_.end()) {
        DatabaseInfo info;
        info.database_name = default_db_;
        databases_[key] = std::move(info);
        LOG_INFO("Catalog: created default database '{}'", default_db_);
    }
    return &databases_[key];
}

bool Catalog::CreateDatabase(const std::string& name, bool if_not_exists) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string key = NormalizeName(name);
    if (databases_.find(key) != databases_.end()) {
        if (if_not_exists) return true;
        LOG_WARN("Catalog: database '{}' already exists", name);
        return false;
    }

    DatabaseInfo info;
    info.database_name = name;  // Store original case
    databases_[key] = std::move(info);

    LOG_INFO("Catalog: created database '{}'", name);
    return true;
}

bool Catalog::DropDatabase(const std::string& name, bool if_exists) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string key = NormalizeName(name);
    auto it = databases_.find(key);
    if (it == databases_.end()) {
        if (if_exists) return true;
        LOG_WARN("Catalog: database '{}' not found", name);
        return false;
    }

    // Cannot drop the default database
    if (key == NormalizeName(default_db_)) {
        LOG_WARN("Catalog: cannot drop the default database '{}'", default_db_);
        return false;
    }

    size_t table_count = it->second.tables.size();
    databases_.erase(it);

    LOG_INFO("Catalog: dropped database '{}' ({} tables)", name, table_count);
    return true;
}

bool Catalog::DatabaseExists(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return databases_.find(NormalizeName(name)) != databases_.end();
}

std::vector<std::string> Catalog::ListDatabases() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(databases_.size());
    for (const auto& [key, info] : databases_) {
        names.push_back(info.database_name);
    }
    return names;
}

// =============================================================================
// Table Operations
// =============================================================================

bool Catalog::CreateTable(const std::string& name, const Schema& schema,
                          const std::string& file_path, page_id_t root_page_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    DatabaseInfo* db = GetOrCreateDefaultDB();

    if (db->tables.find(name) != db->tables.end()) {
        LOG_WARN("Catalog: table '{}' already exists in database '{}'", name, default_db_);
        return false;
    }

    TableInfo info;
    info.table_name = name;
    info.schema = schema;
    info.file_path = file_path;
    info.root_page_id = root_page_id;

    db->tables[name] = std::move(info);

    LOG_INFO("Catalog: created table '{}.{}' with schema {}", default_db_, name, schema.ToString());
    return true;
}

TableInfo* Catalog::GetTable(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    // First search in the default database
    DatabaseInfo* db = GetOrCreateDefaultDB();
    auto it = db->tables.find(name);
    if (it != db->tables.end()) {
        return &it->second;
    }

    // Fallback: search all databases (for backward compatibility)
    for (auto& [db_key, db_info] : databases_) {
        if (db_key == NormalizeName(default_db_)) continue;  // already searched
        auto tit = db_info.tables.find(name);
        if (tit != db_info.tables.end()) {
            return &tit->second;
        }
    }

    return nullptr;
}

bool Catalog::TableExists(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string key = NormalizeName(default_db_);
    auto it = databases_.find(key);
    if (it != databases_.end()) {
        if (it->second.tables.find(name) != it->second.tables.end()) {
            return true;
        }
    }
    return false;
}

bool Catalog::DropTable(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    DatabaseInfo* db = GetOrCreateDefaultDB();
    auto it = db->tables.find(name);
    if (it == db->tables.end()) {
        // Search all databases
        for (auto& [db_key, db_info] : databases_) {
            auto tit = db_info.tables.find(name);
            if (tit != db_info.tables.end()) {
                db_info.tables.erase(tit);
                LOG_INFO("Catalog: dropped table '{}.{}'", db_info.database_name, name);
                return true;
            }
        }
        LOG_WARN("Catalog: table '{}' not found", name);
        return false;
    }

    db->tables.erase(it);
    LOG_INFO("Catalog: dropped table '{}.{}'", default_db_, name);
    return true;
}

std::vector<std::string> Catalog::ListTables() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string key = NormalizeName(default_db_);
    auto it = databases_.find(key);
    if (it == databases_.end()) return {};

    std::vector<std::string> names;
    names.reserve(it->second.tables.size());
    for (const auto& [name, info] : it->second.tables) {
        names.push_back(name);
    }
    return names;
}

std::vector<const TableInfo*> Catalog::GetAllTables() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<const TableInfo*> result;
    for (const auto& [db_key, db_info] : databases_) {
        for (const auto& [name, info] : db_info.tables) {
            result.push_back(&info);
        }
    }
    return result;
}

// =============================================================================
// Index Operations
// =============================================================================

bool Catalog::CreateIndex(const std::string& index_name,
                          const std::string& table_name,
                          const std::string& column_name,
                          IndexInfo::IndexType type,
                          page_id_t root_or_directory_page_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Search all databases for the table
    for (auto& [db_key, db_info] : databases_) {
        auto it = db_info.tables.find(table_name);
        if (it != db_info.tables.end()) {
            // Check index doesn't already exist
            for (const auto& idx : it->second.indexes) {
                if (idx.index_name == index_name) {
                    LOG_WARN("Catalog: index '{}' already exists", index_name);
                    return false;
                }
            }

            IndexInfo idx_info;
            idx_info.index_name = index_name;
            idx_info.table_name = table_name;
            idx_info.column_name = column_name;
            idx_info.type = type;
            idx_info.root_or_directory_page_id = root_or_directory_page_id;

            it->second.indexes.push_back(std::move(idx_info));
            LOG_INFO("Catalog: created {} index '{}' on {}.{}",
                     type == IndexInfo::IndexType::BTREE ? "BTREE" : "HASH",
                     index_name, table_name, column_name);
            return true;
        }
    }

    LOG_WARN("Catalog: table '{}' not found for index '{}'", table_name, index_name);
    return false;
}

IndexInfo* Catalog::GetIndex(const std::string& index_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [db_key, db_info] : databases_) {
        for (auto& [table_name, info] : db_info.tables) {
            for (auto& idx : info.indexes) {
                if (idx.index_name == index_name) return &idx;
            }
        }
    }
    return nullptr;
}

std::vector<IndexInfo*> Catalog::GetTableIndexes(const std::string& table_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<IndexInfo*> result;

    for (auto& [db_key, db_info] : databases_) {
        auto it = db_info.tables.find(table_name);
        if (it != db_info.tables.end()) {
            for (auto& idx : it->second.indexes) {
                result.push_back(&idx);
            }
            return result;
        }
    }
    return result;
}

bool Catalog::DropIndex(const std::string& index_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [db_key, db_info] : databases_) {
        for (auto& [table_name, info] : db_info.tables) {
            auto& indexes = info.indexes;
            for (auto it = indexes.begin(); it != indexes.end(); ++it) {
                if (it->index_name == index_name) {
                    indexes.erase(it);
                    LOG_INFO("Catalog: dropped index '{}'", index_name);
                    return true;
                }
            }
        }
    }
    return false;
}

// =============================================================================
// Persistence
// =============================================================================

bool Catalog::Load() {
    // Catalog persistence: load from goods_db_catalog.db
    // For now, catalog is in-memory only.
    // A full implementation would use TableHeap to persist catalog metadata.
    LOG_INFO("Catalog: loaded (in-memory mode, {} database(s))", databases_.size());
    return true;
}

bool Catalog::Save() {
    // Catalog persistence: save to goods_db_catalog.db
    // For now, catalog is in-memory only.
    LOG_INFO("Catalog: saved (in-memory mode, {} database(s))", databases_.size());
    return true;
}

}  // namespace goods_db
