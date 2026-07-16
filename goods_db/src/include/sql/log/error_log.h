#pragma once

#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace goods_db {

// =============================================================================
// ErrorLog — server error/warning/info log
//
// Thread-safe. Writes timestamped, levelled messages to file and/or stderr.
// Format: [YYYY-MM-DD HH:MM:SS] [LEVEL] [file:line] message
//
// Also maintains an in-memory ring buffer of recent entries for SHOW ERRORLOG.
// =============================================================================
class ErrorLog {
 public:
  enum Level : int {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR_L = 3,  // ERROR conflicts with macro
    FATAL = 4
  };

  struct RingEntry {
    std::string timestamp;
    std::string level;
    int code = 0;
    std::string message;
    std::string source;  // "file:line"
  };

  ErrorLog() = default;
  ~ErrorLog();

  void Initialize(const std::string& file_path, Level min_level = Level::INFO,
                  bool also_stderr = true);
  void Shutdown();

  void Log(Level level, const char* file, int line_number, const char* format, ...)
      __attribute__((format(printf, 5, 6)));

  void SetLevel(Level min_level) { min_level_ = min_level; }
  Level GetLevel() const { return min_level_; }
  bool IsInitialized() const { return initialized_; }

  /// Return up to max_count most recent log entries in chronological order.
  std::vector<RingEntry> GetRecentEntries(size_t max_count = 1000) const;

 private:
  std::ofstream file_;
  mutable std::mutex mutex_;
  Level min_level_ = Level::INFO;
  bool also_stderr_ = true;
  bool initialized_ = false;

  // Ring buffer of recent entries (capacity 2000)
  std::vector<RingEntry> ring_buffer_;
  size_t ring_pos_ = 0;
  static constexpr size_t kRingBufferSize = 2000;

  static const char* LevelToString(Level level);
  static const char* LevelToColor(Level level);
};

}  // namespace goods_db
