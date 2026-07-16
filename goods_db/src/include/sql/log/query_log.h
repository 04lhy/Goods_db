#pragma once

#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace goods_db {

// =============================================================================
// QueryLog — records all executed SQL queries
//
// Format: [timestamp] [user@host] [database] exec_time_ms rows sql_text
// Supports file rotation at configurable max size.
//
// Also maintains an in-memory ring buffer of recent entries for SHOW QUERYLOG.
// =============================================================================
class QueryLog {
 public:
  struct QueryEntry {
    std::string timestamp;
    std::string user;
    std::string host;
    std::string database;
    uint64_t exec_time_ms = 0;
    uint64_t rows_affected = 0;
    std::string sql;
  };

  QueryLog() = default;
  ~QueryLog();

  void Initialize(const std::string& file_path, size_t max_size_mb = 100);
  void Shutdown();

  void LogQuery(const std::string& user, const std::string& host,
                const std::string& database, const std::string& sql,
                uint64_t exec_time_ms, uint64_t rows_affected);

  void Rotate();
  bool IsInitialized() const { return initialized_; }

  /// Return up to max_count most recent query log entries in chronological order.
  std::vector<QueryEntry> GetRecentEntries(size_t max_count = 1000) const;

 private:
  std::ofstream file_;
  mutable std::mutex mutex_;
  std::string file_path_;
  size_t max_size_bytes_ = 100 * 1024 * 1024;
  size_t current_size_ = 0;
  int rotation_index_ = 0;
  bool initialized_ = false;

  // Ring buffer of recent entries (capacity 2000)
  std::vector<QueryEntry> ring_buffer_;
  size_t ring_pos_ = 0;
  static constexpr size_t kRingBufferSize = 2000;

  void CheckRotation();
  std::string GetRotationPath(int index) const;
};

}  // namespace goods_db
