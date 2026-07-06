#include "catalog/catalog.h"
#include "common/logger.h"

namespace goods_db {

Catalog::Catalog(BufferPoolManager* bpm) : bpm_(bpm) {}

bool Catalog::CreateTable(const std::string& name, const Schema& schema,
                           const std::string& file_path, page_id_t root_page_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (tables_.find(name) != tables_.end()) {
        LOG_WARN("Catalog: table '{}' already exists", name);
        return false;
    }

    TableInfo info;
    info.table_name = name;
    info.schema = schema;
    info.file_path = file_path;
    info.root_page_id = root_page_id;

    tables_[name] = std::move(info);

    LOG_INFO("Catalog: created table '{}' with schema {}", name, schema.ToString());
    return true;
}

TableInfo* Catalog::GetTable(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tables_.find(name);
    if (it == tables_.end()) return nullptr;
    return &it->second;
}

bool Catalog::TableExists(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tables_.find(name) != tables_.end();
}

bool Catalog::DropTable(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tables_.find(name);
    if (it == tables_.end()) return false;

    tables_.erase(it);
    LOG_INFO("Catalog: dropped table '{}'", name);
    return true;
}

std::vector<std::string> Catalog::ListTables() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(tables_.size());
    for (const auto& [name, info] : tables_) {
        names.push_back(name);
    }
    return names;
}

bool Catalog::CreateIndex(const std::string& index_name,
                           const std::string& table_name,
                           const std::string& column_name,
                           IndexInfo::IndexType type,
                           page_id_t root_or_directory_page_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = tables_.find(table_name);
    if (it == tables_.end()) {
        LOG_WARN("Catalog: table '{}' not found for index '{}'", table_name, index_name);
        return false;
    }

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

IndexInfo* Catalog::GetIndex(const std::string& index_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [table_name, info] : tables_) {
        for (auto& idx : info.indexes) {
            if (idx.index_name == index_name) return &idx;
        }
    }
    return nullptr;
}

std::vector<IndexInfo*> Catalog::GetTableIndexes(const std::string& table_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<IndexInfo*> result;

    auto it = tables_.find(table_name);
    if (it == tables_.end()) return result;

    for (auto& idx : it->second.indexes) {
        result.push_back(&idx);
    }
    return result;
}

bool Catalog::DropIndex(const std::string& index_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [table_name, info] : tables_) {
        auto& indexes = info.indexes;
        for (auto it = indexes.begin(); it != indexes.end(); ++it) {
            if (it->index_name == index_name) {
                indexes.erase(it);
                LOG_INFO("Catalog: dropped index '{}'", index_name);
                return true;
            }
        }
    }
    return false;
}

bool Catalog::Load() {
    // Catalog persistence: load from goods_db_catalog.db
    // For now, catalog is in-memory only.
    // A full implementation would use TableHeap to persist catalog metadata.
    LOG_INFO("Catalog: loaded (in-memory mode)");
    return true;
}

bool Catalog::Save() {
    // Catalog persistence: save to goods_db_catalog.db
    // For now, catalog is in-memory only.
    LOG_INFO("Catalog: saved (in-memory mode)");
    return true;
}

}  // namespace goods_db
