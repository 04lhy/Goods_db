#include "sql/log/query_log.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace goods_db {

QueryLog::~QueryLog() {
  Shutdown();
}

void QueryLog::Initialize(const std::string& file_path, size_t max_size_mb) {
  std::lock_guard<std::mutex> lock(mutex_);
  file_path_ = file_path;
  max_size_bytes_ = max_size_mb * 1024 * 1024;

  file_.open(file_path_, std::ios::out | std::ios::app);
  if (!file_.is_open()) {
    return;
  }

  // Get current file size
  file_.seekp(0, std::ios::end);
  current_size_ = file_.tellp();

  initialized_ = true;
}

void QueryLog::Shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (initialized_ && file_.is_open()) {
    file_.close();
  }
  initialized_ = false;
}

void QueryLog::LogQuery(const std::string& user, const std::string& host,
                         const std::string& database, const std::string& sql,
                         uint64_t exec_time_ms, uint64_t rows_affected) {
  CheckRotation();

  // Format timestamp
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) % 1000;

  std::tm tm_buf;
  localtime_r(&time_t, &tm_buf);

  char time_buf[32];
  std::snprintf(time_buf, sizeof(time_buf),
                "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                static_cast<int>(ms.count()));

  std::ostringstream line;
  line << "[" << time_buf << "] "
       << "[" << user << "@" << host << "] "
       << "[" << (database.empty() ? "(none)" : database) << "] "
       << "[" << exec_time_ms << "ms] "
       << "[" << rows_affected << " rows] "
       << sql << "\n";

  std::lock_guard<std::mutex> lock(mutex_);

  // Append to ring buffer
  {
    QueryEntry entry;
    entry.timestamp = std::string(time_buf);
    entry.user = user;
    entry.host = host;
    entry.database = database.empty() ? "(none)" : database;
    entry.exec_time_ms = exec_time_ms;
    entry.rows_affected = rows_affected;
    entry.sql = sql;

    if (ring_buffer_.size() < kRingBufferSize) {
      ring_buffer_.push_back(std::move(entry));
    } else {
      ring_buffer_[ring_pos_] = std::move(entry);
      ring_pos_ = (ring_pos_ + 1) % kRingBufferSize;
    }
  }

  if (!file_.is_open()) {
    return;
  }

  std::string line_str = line.str();
  file_ << line_str;
  file_.flush();
  current_size_ += line_str.size();
}

std::vector<QueryLog::QueryEntry> QueryLog::GetRecentEntries(
    size_t max_count) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<QueryEntry> result;
  size_t total = ring_buffer_.size();
  size_t count = std::min(max_count, total);
  result.reserve(count);

  if (total <= kRingBufferSize) {
    // Ring hasn't wrapped yet — entries are in linear order
    size_t start = total > count ? total - count : 0;
    for (size_t i = start; i < total; i++)
      result.push_back(ring_buffer_[i]);
  } else {
    // Ring has wrapped — start from ring_pos_ (oldest entry)
    for (size_t i = 0; i < count; i++)
      result.push_back(ring_buffer_[(ring_pos_ + i) % kRingBufferSize]);
  }
  return result;
}

void QueryLog::Rotate() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (file_.is_open()) {
    file_.close();
  }

  // Rename current file
  rotation_index_++;
  std::string rotated = GetRotationPath(rotation_index_);
  std::rename(file_path_.c_str(), rotated.c_str());

  // Open new file
  file_.open(file_path_, std::ios::out | std::ios::trunc);
  current_size_ = 0;
}

void QueryLog::CheckRotation() {
  if (current_size_ >= max_size_bytes_) {
    Rotate();
  }
}

std::string QueryLog::GetRotationPath(int index) const {
  return file_path_ + "." + std::to_string(index);
}

}  // namespace goods_db
