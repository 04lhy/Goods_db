#pragma once

#include <fstream>
#include <mutex>
#include <string>

namespace goods_db {

// =============================================================================
// ErrorLog — server error/warning/info log
//
// Thread-safe. Writes timestamped, levelled messages to file and/or stderr.
// Format: [YYYY-MM-DD HH:MM:SS] [LEVEL] [file:line] message
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

 private:
  std::ofstream file_;
  std::mutex mutex_;
  Level min_level_ = Level::INFO;
  bool also_stderr_ = true;
  bool initialized_ = false;

  static const char* LevelToString(Level level);
  static const char* LevelToColor(Level level);
};

}  // namespace goods_db
