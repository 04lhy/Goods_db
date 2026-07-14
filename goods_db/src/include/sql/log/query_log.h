#pragma once

#include <fstream>
#include <mutex>
#include <string>

namespace goods_db {

// =============================================================================
// QueryLog — records all executed SQL queries
//
// Format: [timestamp] [user@host] [database] exec_time_ms rows sql_text
// Supports file rotation at configurable max size.
// =============================================================================
class QueryLog {
 public:
  QueryLog() = default;
  ~QueryLog();

  void Initialize(const std::string& file_path, size_t max_size_mb = 100);
  void Shutdown();

  void LogQuery(const std::string& user, const std::string& host,
                const std::string& database, const std::string& sql,
                uint64_t exec_time_ms, uint64_t rows_affected);

  void Rotate();
  bool IsInitialized() const { return initialized_; }

 private:
  std::ofstream file_;
  std::mutex mutex_;
  std::string file_path_;
  size_t max_size_bytes_ = 100 * 1024 * 1024;
  size_t current_size_ = 0;
  int rotation_index_ = 0;
  bool initialized_ = false;

  void CheckRotation();
  std::string GetRotationPath(int index) const;
};

}  // namespace goods_db
