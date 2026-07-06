#include "sql/goods_handler.h"
#include "common/config.h"
#include "common/logger.h"
#include "storage/index/index.h"

namespace goods_db {

// =============================================================================
// Static engine-level state (shared singletons)
// =============================================================================

static std::unique_ptr<BufferPoolManager> g_bpm;
static std::unique_ptr<DiskManager> g_disk_manager;
static std::unique_ptr<Catalog> g_catalog;

// =============================================================================
// goods_handler Methods
// =============================================================================

goods_handler::goods_handler(BufferPoolManager* bpm, DiskManager* disk_manager,
                             Catalog* catalog)
    : bpm_(bpm), disk_manager_(disk_manager), catalog_(catalog) {}

goods_handler::~goods_handler() {
    close();
}

// =============================================================================
// DDL
// =============================================================================

int goods_handler::create(const char* name, Schema* schema) {
    std::string table_name(name);
    LOG_INFO("goods_handler::create table '{}'", table_name);

    // Create the .db file
    std::string file_path = disk_manager_->GetDataDir() + "/" + table_name + ".db";
    uint16_t file_id = disk_manager_->CreateFile(table_name, file_path);
    if (file_id == static_cast<uint16_t>(-1)) {
        LOG_ERROR("Failed to create file for table '{}'", table_name);
        return -1;
    }

    // Allocate first page (page 1, since page 0 is the file header)
    page_id_t root_page_id = disk_manager_->AllocatePage(file_id);
    if (root_page_id == INVALID_PAGE_ID) {
        LOG_ERROR("Failed to allocate first page for table '{}'", table_name);
        return -1;
    }

    // Initialize the first page as a TablePage
    Page* page = bpm_->FetchPage(root_page_id);
    if (!page) return -1;

    page->WLatch();
    std::memset(page->GetData(), 0, PAGE_SIZE);
    TablePage table_page;
    table_page.LoadFromData(page->GetData());
    table_page.Init(root_page_id);
    page->MarkDirty();
    page->WUnlatch();
    bpm_->UnpinPage(root_page_id, true);

    // Create a TableHeap
    auto table_heap = std::make_unique<TableHeap>(bpm_, root_page_id);

    // Register in catalog
    bool ok = catalog_->CreateTable(table_name, *schema, file_path, root_page_id);
    if (!ok) {
        LOG_ERROR("Catalog registration failed for table '{}'", table_name);
        return -1;
    }

    // Store TableHeap in catalog
    TableInfo* info = catalog_->GetTable(table_name);
    if (info) {
        info->table_heap = std::move(table_heap);
    }

    LOG_INFO("Table '{}' created successfully (file={}, root_page={})",
             table_name, file_path, root_page_id);
    return 0;
}

int goods_handler::open(const char* name) {
    std::string table_name(name);
    table_name_ = table_name;

    table_info_ = catalog_->GetTable(table_name_);
    if (!table_info_) {
        LOG_ERROR("Table '{}' not found in catalog", table_name_);
        return -1;
    }

    schema_ = &table_info_->schema;
    table_heap_ = table_info_->table_heap.get();

    if (!table_heap_) {
        LOG_ERROR("TableHeap not initialized for table '{}'", table_name_);
        return -1;
    }

    LOG_INFO("Table '{}' opened", table_name_);
    return 0;
}

int goods_handler::close() {
    if (scan_active_) {
        rnd_end();
    }
    if (index_scan_active_) {
        index_end();
    }
    table_name_.clear();
    schema_ = nullptr;
    table_info_ = nullptr;
    table_heap_ = nullptr;
    return 0;
}

int goods_handler::delete_table(const char* name) {
    std::string table_name(name);

    // Close if open
    if (table_name_ == table_name) {
        close();
    }

    // Flush dirty pages and delete file
    TableInfo* info = catalog_->GetTable(table_name);
    if (info) {
        uint16_t file_id = GetFileId(info->root_page_id);
        bpm_->FlushFilePages(file_id);
        disk_manager_->DeleteFile(file_id, info->file_path);
    }

    // Remove from catalog
    catalog_->DropTable(table_name);

    LOG_INFO("Table '{}' deleted", table_name);
    return 0;
}

// =============================================================================
// Full Table Scan
// =============================================================================

int goods_handler::rnd_init(bool scan_all) {
    if (!table_heap_) return -1;
    if (scan_active_) rnd_end();

    table_iterator_ = table_heap_->MakeIterator();
    scan_active_ = true;
    return 0;
}

int goods_handler::rnd_next(Tuple* buf) {
    if (!scan_active_ || !table_iterator_) return -1;
    if (!table_iterator_->HasNext()) return -1;

    auto [rid, tuple] = table_iterator_->Next();
    *buf = std::move(tuple);
    return 0;
}

int goods_handler::rnd_pos(Tuple* buf, RID rid) {
    if (!table_heap_) return -1;
    if (!schema_) return -1;
    return table_heap_->GetTuple(rid, buf, schema_) ? 0 : -1;
}

int goods_handler::rnd_end() {
    table_iterator_.reset();
    scan_active_ = false;
    return 0;
}

// =============================================================================
// Index Scan
// =============================================================================

int goods_handler::index_init(uint32_t idx_id, bool sorted) {
    if (!table_info_) return -1;
    if (idx_id >= table_info_->indexes.size()) return -1;

    if (index_scan_active_) index_end();

    active_index_id_ = idx_id;
    active_index_ = table_info_->indexes[idx_id].index_instance.get();
    index_scan_active_ = true;
    index_scan_pos_ = 0;
    return 0;
}

int goods_handler::index_read(Tuple* buf, const Value* key) {
    if (!index_scan_active_ || !active_index_ || !table_heap_ || !schema_)
        return -1;

    int64_t search_key = 0;
    switch (key->GetTypeId()) {
        case TypeId::INTEGER: search_key = key->GetAsInteger(); break;
        case TypeId::BIGINT:  search_key = key->GetAsBigInt(); break;
        case TypeId::SMALLINT: search_key = key->GetAsSmallInt(); break;
        case TypeId::TINYINT: search_key = key->GetAsTinyInt(); break;
        default: return -1;
    }

    RID rid = active_index_->GetValue(search_key);
    if (rid.page_id == 0 && rid.slot_id == 0) return -1;

    return table_heap_->GetTuple(rid, buf, schema_) ? 0 : -1;
}

int goods_handler::index_next(Tuple* buf) {
    if (!index_scan_active_ || !active_index_ || !table_heap_ || !schema_)
        return -1;
    if (index_scan_pos_ >= index_scan_results_.size()) return -1;

    auto& [key, rid] = index_scan_results_[index_scan_pos_++];
    return table_heap_->GetTuple(rid, buf, schema_) ? 0 : -1;
}

int goods_handler::index_end() {
    index_scan_active_ = false;
    active_index_ = nullptr;
    index_scan_results_.clear();
    index_scan_pos_ = 0;
    return 0;
}

// =============================================================================
// Row-level DML
// =============================================================================

int goods_handler::write_row(const Tuple& tuple) {
    if (!table_heap_ || !schema_) return -1;

    RID rid;
    if (!table_heap_->InsertTuple(tuple, &rid)) return -1;

    // Update all indexes
    for (auto& idx_info : table_info_->indexes) {
        if (!idx_info.index_instance) continue;
        // Get the key value from the tuple
        int32_t col_idx = schema_->GetColIdx(idx_info.column_name);
        if (col_idx < 0) continue;

        Value key_val = tuple.GetValue(schema_, col_idx);
        int64_t key = 0;
        switch (key_val.GetTypeId()) {
            case TypeId::INTEGER: key = key_val.GetAsInteger(); break;
            case TypeId::BIGINT:  key = key_val.GetAsBigInt(); break;
            case TypeId::SMALLINT: key = key_val.GetAsSmallInt(); break;
            case TypeId::TINYINT: key = key_val.GetAsTinyInt(); break;
            default: continue;
        }
        idx_info.index_instance->Insert(key, rid);
    }

    return 0;
}

int goods_handler::update_row(const RID& rid, const Tuple& new_tuple) {
    if (!table_heap_ || !schema_) return -1;

    RID new_rid;
    if (!table_heap_->UpdateTuple(rid, new_tuple, &new_rid)) return -1;

    // Update index entries if RID changed
    if (new_rid != rid) {
        for (auto& idx_info : table_info_->indexes) {
            if (!idx_info.index_instance) continue;
            // Simplified: remove old, insert new
            // Full implementation would track old key values
        }
    }

    return 0;
}

int goods_handler::delete_row(const RID& rid) {
    if (!table_heap_) return -1;

    // Remove from all indexes (simplified)
    for (auto& idx_info : table_info_->indexes) {
        if (!idx_info.index_instance) continue;
        // Would need to find the key for this RID
    }

    return table_heap_->MarkDelete(rid) ? 0 : -1;
}

// =============================================================================
// Information
// =============================================================================

uint64_t goods_handler::records() {
    if (!table_heap_) return 0;
    auto it = table_heap_->MakeIterator();
    return it->Count();
}

uint64_t goods_handler::table_flags() const {
    return HTON_FLAG_SUPPORTS_INDEXES | HTON_FLAG_SUPPORTS_BACKUP;
}

const char* goods_handler::get_table_name() const {
    return table_name_.c_str();
}

const Schema* goods_handler::get_schema() const {
    return schema_;
}

// =============================================================================
// Engine Registration Helpers
// =============================================================================

int goods_handler::engine_init() {
    LOG_INFO("goods_engine initializing...");

    g_disk_manager = std::make_unique<DiskManager>();
    g_disk_manager->SetDataDir(DEFAULT_DATA_DIR);

    // Create data directory
    std::string cmd = "mkdir -p " + std::string(DEFAULT_DATA_DIR);
    system(cmd.c_str());

    auto replacer = std::make_unique<ClockReplacer>(BUFFER_POOL_DEFAULT_FRAMES);
    g_bpm = std::make_unique<BufferPoolManager>(
        BUFFER_POOL_DEFAULT_FRAMES, g_disk_manager.get(), std::move(replacer));

    g_catalog = std::make_unique<Catalog>(g_bpm.get());
    g_catalog->Load();

    LOG_INFO("goods_engine initialized (buffer_pool={} frames, data_dir={})",
             BUFFER_POOL_DEFAULT_FRAMES, DEFAULT_DATA_DIR);
    return 0;
}

int goods_handler::engine_deinit() {
    LOG_INFO("goods_engine shutting down...");

    if (g_catalog) {
        g_catalog->Save();
        g_catalog.reset();
    }

    if (g_bpm) {
        g_bpm->FlushAllPages();
        g_bpm.reset();
    }

    g_disk_manager.reset();

    LOG_INFO("goods_engine shutdown complete");
    return 0;
}

handler* goods_handler::engine_create_handler(const char* table_name,
                                               Schema* schema) {
    return new goods_handler(g_bpm.get(), g_disk_manager.get(), g_catalog.get());
}

BufferPoolManager* goods_handler::GetSharedBPM() { return g_bpm.get(); }
DiskManager* goods_handler::GetSharedDiskManager() { return g_disk_manager.get(); }
Catalog* goods_handler::GetSharedCatalog() { return g_catalog.get(); }

}  // namespace goods_db
