#include "sql/log/log_manager.h"

namespace goods_db {

LogManager::~LogManager() {
  Shutdown();
}

void LogManager::Initialize(const std::string& base_dir) {
  if (initialized_) return;

  std::string err_path = base_dir + "/goods_db_error.log";
  std::string query_path = base_dir + "/goods_db_query.log";
  std::string binlog_prefix = base_dir + "/goods_db_binlog";

  error_log_.Initialize(err_path);
  query_log_.Initialize(query_path);
  binlog_writer_.Initialize(binlog_prefix);

  initialized_ = true;
}

void LogManager::Shutdown() {
  if (!initialized_) return;

  binlog_writer_.Shutdown();
  query_log_.Shutdown();
  error_log_.Shutdown();

  initialized_ = false;
}

}  // namespace goods_db
