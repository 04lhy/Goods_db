#include "sql/log/error_log.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace goods_db {

ErrorLog::~ErrorLog() {
  Shutdown();
}

void ErrorLog::Initialize(const std::string& file_path, Level min_level,
                           bool also_stderr) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    min_level_ = min_level;
    also_stderr_ = also_stderr;

    file_.open(file_path, std::ios::out | std::ios::app);
    if (!file_.is_open()) {
      std::cerr << "ErrorLog: failed to open " << file_path << std::endl;
      return;
    }

    initialized_ = true;
  }
  // Log without holding the lock to avoid deadlock
  Log(INFO, __FILE__, __LINE__, "ErrorLog initialized, file=%s", file_path.c_str());
}

void ErrorLog::Shutdown() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return;
    initialized_ = false;
  }
  Log(INFO, __FILE__, __LINE__, "ErrorLog shutting down");
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
      file_.close();
    }
  }
}

void ErrorLog::Log(Level level, const char* file, int line_number,
                    const char* format, ...) {
  if (level < min_level_) {
    return;
  }

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

  // Get thread id
  std::ostringstream thread_id;
  thread_id << std::this_thread::get_id();

  // Format message
  char msg_buf[4096];
  va_list args;
  va_start(args, format);
  std::vsnprintf(msg_buf, sizeof(msg_buf), format, args);
  va_end(args);

  // Build log line
  std::ostringstream line;
  line << "[" << time_buf << "] "
       << "[" << LevelToString(level) << "] "
       << "[" << thread_id.str() << "] "
       << "[" << file << ":" << line_number << "] "
       << msg_buf << "\n";

  std::lock_guard<std::mutex> lock(mutex_);

  if (file_.is_open()) {
    file_ << line.str();
    file_.flush();
  }

  if (also_stderr_) {
    std::cerr << line.str();
  }

  // Append to ring buffer
  {
    RingEntry entry;
    entry.timestamp = std::string(time_buf);
    entry.level = LevelToString(level);
    entry.message = std::string(msg_buf);
    entry.source = std::string(file) + ":" + std::to_string(line_number);

    if (ring_buffer_.size() < kRingBufferSize) {
      ring_buffer_.push_back(std::move(entry));
    } else {
      ring_buffer_[ring_pos_] = std::move(entry);
      ring_pos_ = (ring_pos_ + 1) % kRingBufferSize;
    }
  }

  // FATAL: abort
  if (level == FATAL) {
    std::abort();
  }
}

std::vector<ErrorLog::RingEntry> ErrorLog::GetRecentEntries(
    size_t max_count) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<RingEntry> result;
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

const char* ErrorLog::LevelToString(Level level) {
  switch (level) {
    case DEBUG:   return "DEBUG";
    case INFO:    return "INFO";
    case WARN:    return "WARN";
    case ERROR_L: return "ERROR";
    case FATAL:   return "FATAL";
    default:      return "UNKNOWN";
  }
}

}  // namespace goods_db
