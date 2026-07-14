#pragma once

#include <string>

#include "sql/log/error_log.h"
#include "sql/log/query_log.h"
#include "sql/log/binary_log.h"

namespace goods_db {

// =============================================================================
// LogManager — owns and coordinates all three log subsystems
// =============================================================================
class LogManager {
 public:
  LogManager() = default;
  ~LogManager();

  void Initialize(const std::string& base_dir = ".");
  void Shutdown();

  ErrorLog& GetErrorLog() { return error_log_; }
  QueryLog& GetQueryLog() { return query_log_; }
  BinlogWriter& GetBinlogWriter() { return binlog_writer_; }

  bool IsInitialized() const { return initialized_; }

 private:
  ErrorLog error_log_;
  QueryLog query_log_;
  BinlogWriter binlog_writer_;
  bool initialized_ = false;
};

}  // namespace goods_db
