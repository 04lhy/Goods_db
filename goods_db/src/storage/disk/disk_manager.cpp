#include "storage/disk/disk_manager.h"
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include "common/logger.h"

namespace goods_db {

// File header layout for page 0 of each .db file
struct FileHeader {
    char table_name[64];       // table name
    uint16_t total_pages;      // total number of pages in the file
    uint16_t first_free_page;  // first free page in the free page chain (0 if none)
    char reserved[PAGE_SIZE - 64 - 2 - 2];  // padding to PAGE_SIZE
};

static_assert(sizeof(FileHeader) == PAGE_SIZE, "FileHeader must be PAGE_SIZE");

DiskManager::~DiskManager() {
    // Close all open files
    for (auto& [file_id, entry] : files_) {
        if (entry.stream.is_open()) {
            entry.stream.close();
        }
    }
    files_.clear();
}

uint16_t DiskManager::CreateFile(const std::string& table_name,
                                  const std::string& db_path) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if file already exists
    std::fstream test(db_path, std::ios::in | std::ios::binary);
    if (test.is_open()) {
        test.close();
        LOG_WARN("File already exists: {}", db_path);
        return OpenFile(table_name, db_path);
    }

    // Create directory if needed
    size_t sep = db_path.find_last_of('/');
    if (sep != std::string::npos) {
        std::string dir = db_path.substr(0, sep);
        mkdir(dir.c_str(), 0755);
    }

    // Create the file
    std::fstream stream(db_path, std::ios::out | std::ios::in |
                                  std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        LOG_ERROR("Failed to create file: {}", db_path);
        return static_cast<uint16_t>(-1);
    }

    // Initialize file header (page 0)
    FileHeader header{};
    header.total_pages = 1;  // includes the header page
    header.first_free_page = 0;
    std::strncpy(header.table_name, table_name.c_str(), 63);
    header.table_name[63] = '\0';

    stream.seekp(0);
    stream.write(reinterpret_cast<const char*>(&header), PAGE_SIZE);
    stream.flush();

    // Register the file
    uint16_t file_id = next_file_id_++;
    FileEntry entry;
    entry.table_name = table_name;
    entry.db_path = db_path;
    entry.stream = std::move(stream);
    entry.num_pages = 1;

    files_[file_id] = std::move(entry);

    LOG_INFO("Created file '{}' (id={}) with 1 page", db_path, file_id);
    return file_id;
}

uint16_t DiskManager::OpenFile(const std::string& table_name,
                                const std::string& db_path) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if already open
    for (const auto& [fid, entry] : files_) {
        if (entry.db_path == db_path) return fid;
    }

    std::fstream stream(db_path, std::ios::in | std::ios::out | std::ios::binary);
    if (!stream.is_open()) {
        LOG_ERROR("Failed to open file: {}", db_path);
        return static_cast<uint16_t>(-1);
    }

    // Read file header to get total pages
    stream.seekg(0, std::ios::end);
    size_t file_size = stream.tellg();
    uint16_t num_pages = static_cast<uint16_t>(file_size / PAGE_SIZE);

    uint16_t file_id = next_file_id_++;
    FileEntry entry;
    entry.table_name = table_name;
    entry.db_path = db_path;
    entry.stream = std::move(stream);
    entry.num_pages = num_pages;

    files_[file_id] = std::move(entry);

    LOG_INFO("Opened file '{}' (id={}) with {} pages", db_path, file_id, num_pages);
    return file_id;
}

void DiskManager::CloseFile(uint16_t file_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(file_id);
    if (it != files_.end()) {
        it->second.stream.close();
        files_.erase(it);
        LOG_INFO("Closed file id={}", file_id);
    }
}

void DiskManager::DeleteFile(uint16_t file_id, const std::string& db_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(file_id);
    if (it != files_.end()) {
        it->second.stream.close();
        files_.erase(it);
    }
    std::remove(db_path.c_str());
    LOG_INFO("Deleted file: {}", db_path);
}

void DiskManager::FlushFile(uint16_t file_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(file_id);
    if (it != files_.end() && it->second.stream.is_open()) {
        it->second.stream.flush();
    }
}

std::string DiskManager::GetTableName(uint16_t file_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(file_id);
    if (it != files_.end()) return it->second.table_name;
    return "";
}

uint16_t DiskManager::GetNumPages(uint16_t file_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(file_id);
    if (it != files_.end()) return it->second.num_pages;
    return 0;
}

bool DiskManager::ReadPage(page_id_t page_id, char* page_data) {
    uint16_t file_id = GetFileId(page_id);
    uint16_t page_no = GetPageNo(page_id);

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(file_id);
    if (it == files_.end() || !it->second.stream.is_open()) {
        LOG_ERROR("ReadPage: invalid file_id={}", file_id);
        return false;
    }

    auto& stream = it->second.stream;
    std::streamoff offset = static_cast<std::streamoff>(page_no) * PAGE_SIZE;

    stream.seekg(offset);
    stream.read(page_data, PAGE_SIZE);

    if (stream.fail()) {
        LOG_ERROR("ReadPage: failed to read page_id={} (file={}, page={})",
                  page_id, file_id, page_no);
        stream.clear();
        return false;
    }

    return true;
}

bool DiskManager::WritePage(page_id_t page_id, const char* page_data) {
    uint16_t file_id = GetFileId(page_id);
    uint16_t page_no = GetPageNo(page_id);

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(file_id);
    if (it == files_.end() || !it->second.stream.is_open()) {
        LOG_ERROR("WritePage: invalid file_id={}", file_id);
        return false;
    }

    auto& stream = it->second.stream;
    std::streamoff offset = static_cast<std::streamoff>(page_no) * PAGE_SIZE;

    stream.seekp(offset);
    stream.write(page_data, PAGE_SIZE);

    if (stream.fail()) {
        LOG_ERROR("WritePage: failed to write page_id={} (file={}, page={})",
                  page_id, file_id, page_no);
        stream.clear();
        return false;
    }

    return true;
}

page_id_t DiskManager::AllocatePage(uint16_t file_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(file_id);
    if (it == files_.end()) {
        LOG_ERROR("AllocatePage: invalid file_id={}", file_id);
        return INVALID_PAGE_ID;
    }

    auto& entry = it->second;

    // Check free page chain first
    // Read page 0 header
    FileHeader header;
    entry.stream.seekg(0);
    entry.stream.read(reinterpret_cast<char*>(&header), PAGE_SIZE);

    if (header.first_free_page != 0) {
        // Reuse a freed page
        uint16_t page_no = header.first_free_page;

        // Read the freed page to get the next free page pointer
        char free_page_data[PAGE_SIZE];
        entry.stream.seekg(static_cast<std::streamoff>(page_no) * PAGE_SIZE);
        entry.stream.read(free_page_data, PAGE_SIZE);

        // First 2 bytes of a freed page stores next_free_page
        uint16_t next_free;
        std::memcpy(&next_free, free_page_data, 2);

        header.first_free_page = next_free;
        entry.stream.seekp(0);
        entry.stream.write(reinterpret_cast<const char*>(&header), PAGE_SIZE);
        entry.stream.flush();

        return MakePageId(file_id, page_no);
    }

    // No free pages: grow the file
    uint16_t page_no = entry.num_pages;
    entry.num_pages++;

    // Update header
    header.total_pages = entry.num_pages;
    entry.stream.seekp(0);
    entry.stream.write(reinterpret_cast<const char*>(&header), PAGE_SIZE);

    // Extend the file by writing a zeroed page at the end
    char zero_page[PAGE_SIZE]{};
    entry.stream.seekp(static_cast<std::streamoff>(page_no) * PAGE_SIZE);
    entry.stream.write(zero_page, PAGE_SIZE);
    entry.stream.flush();

    return MakePageId(file_id, page_no);
}

void DiskManager::DeallocatePage(page_id_t page_id) {
    uint16_t file_id = GetFileId(page_id);
    uint16_t page_no = GetPageNo(page_id);

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(file_id);
    if (it == files_.end()) return;

    auto& entry = it->second;

    // Read page 0 header
    FileHeader header;
    entry.stream.seekg(0);
    entry.stream.read(reinterpret_cast<char*>(&header), PAGE_SIZE);

    // Add this page to the front of the free page chain
    char free_page_data[PAGE_SIZE]{};
    std::memcpy(free_page_data, &header.first_free_page, 2);

    entry.stream.seekp(static_cast<std::streamoff>(page_no) * PAGE_SIZE);
    entry.stream.write(free_page_data, PAGE_SIZE);

    header.first_free_page = page_no;
    entry.stream.seekp(0);
    entry.stream.write(reinterpret_cast<const char*>(&header), PAGE_SIZE);
    entry.stream.flush();
}

bool DiskManager::SyncFile(uint16_t file_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(file_id);
    if (it == files_.end()) return false;

    it->second.stream.flush();
    // fsync would require platform-specific code
    return true;
}

DiskManager::FileEntry* DiskManager::GetFileEntry(uint16_t file_id) {
    auto it = files_.find(file_id);
    if (it == files_.end()) return nullptr;
    return &it->second;
}

}  // namespace goods_db
