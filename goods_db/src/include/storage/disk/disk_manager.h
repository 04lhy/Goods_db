#pragma once

#include <cstdint>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include "common/config.h"

namespace goods_db {

/**
 * Page ID: encodes file_id + page index within file.
 *
 * Layout (32 bits):
 *   - bits 31..16: file_id (up to 65535 files)
 *   - bits 15..0:  page_no (up to 65535 pages per file, = 1GB per file at 16KB/page)
 */

/** Extract file_id from page_id */
inline uint16_t GetFileId(page_id_t page_id) {
    return static_cast<uint16_t>((page_id >> 16) & 0xFFFF);
}

/** Extract page_no from page_id */
inline uint16_t GetPageNo(page_id_t page_id) {
    return static_cast<uint16_t>(page_id & 0xFFFF);
}

/** Create page_id from file_id and page_no */
inline page_id_t MakePageId(uint16_t file_id, uint16_t page_no) {
    return (static_cast<int32_t>(file_id) << 16) | page_no;
}

/**
 * DiskManager: manages disk I/O for database pages.
 *
 * Each table gets its own .db file on disk.
 * File structure:
 *   - Page 0: file header (table name, total pages, first free page chain head)
 *   - Pages 1..N: data pages
 *
 * The DiskManager maintains a mapping of table names to file descriptors.
 */
class DiskManager {
public:
    DiskManager() = default;
    ~DiskManager();

    // Non-copyable, non-movable
    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;

    // =========================================================================
    // File Management (for DDL operations)
    // =========================================================================

    /**
     * Create a new database file for a table.
     * Creates the file and initializes page 0 (file header).
     * @param table_name The table name
     * @param db_path Full path to the .db file
     * @return file_id on success, -1 on failure
     */
    uint16_t CreateFile(const std::string& table_name, const std::string& db_path);

    /**
     * Open an existing database file.
     * @param table_name The table name
     * @param db_path Full path to the .db file
     * @return file_id on success, -1 on failure
     */
    uint16_t OpenFile(const std::string& table_name, const std::string& db_path);

    /**
     * Close a database file and remove from mapping.
     */
    void CloseFile(uint16_t file_id);

    /**
     * Delete a database file from disk.
     */
    void DeleteFile(uint16_t file_id, const std::string& db_path);

    /**
     * Flush all buffered writes for a file.
     */
    void FlushFile(uint16_t file_id);

    /**
     * Get the table name associated with a file_id.
     */
    std::string GetTableName(uint16_t file_id) const;

    /**
     * Get number of total pages in a file.
     */
    uint16_t GetNumPages(uint16_t file_id) const;

    // =========================================================================
    // Page I/O Operations
    // =========================================================================

    /**
     * Read a page from disk into memory buffer.
     * @param page_id Full page identifier
     * @param page_data Destination buffer (must be PAGE_SIZE bytes)
     * @return true on success
     */
    bool ReadPage(page_id_t page_id, char* page_data);

    /**
     * Write a page from memory buffer to disk.
     * @param page_id Full page identifier
     * @param page_data Source buffer (must be PAGE_SIZE bytes)
     * @return true on success
     */
    bool WritePage(page_id_t page_id, const char* page_data);

    /**
     * Allocate a new page for a file. Returns the new page_id.
     * Grows the file by one page.
     */
    page_id_t AllocatePage(uint16_t file_id);

    /**
     * Deallocate a page (mark it as free in the file header).
     */
    void DeallocatePage(page_id_t page_id);

    /**
     * Synchronize a file to disk (fsync).
     */
    bool SyncFile(uint16_t file_id);

    /**
     * Set the data directory for database files.
     */
    void SetDataDir(const std::string& data_dir) { data_dir_ = data_dir; }

    const std::string& GetDataDir() const { return data_dir_; }

private:
    /** File entry tracking */
    struct FileEntry {
        std::string table_name;
        std::string db_path;
        std::fstream stream;
        uint16_t num_pages{0};
    };

    /** Get or assert file entry exists */
    FileEntry* GetFileEntry(uint16_t file_id);

    std::string data_dir_{"./data"};
    std::map<uint16_t, FileEntry> files_;
    uint16_t next_file_id_{0};
    mutable std::mutex mutex_;
};

}  // namespace goods_db
