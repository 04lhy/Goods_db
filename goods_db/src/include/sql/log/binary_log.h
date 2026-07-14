#pragma once

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace goods_db {

// =============================================================================
// Binary Log — WAL-based replication log (binlog)
//
// Records all data-modifying operations as binlog events.
// Supports file rotation and incremental backup via position tracking.
// =============================================================================

enum class BinlogEventType : uint8_t {
  UNKNOWN = 0x00,
  QUERY = 0x01,
  ROW_INSERT = 0x10,
  ROW_UPDATE = 0x11,
  ROW_DELETE = 0x12,
  XID = 0x20,
};

struct BinlogEvent {
  uint64_t timestamp = 0;
  uint32_t server_id = 1;
  BinlogEventType event_type = BinlogEventType::UNKNOWN;
  uint64_t txn_id = 0;
  std::vector<uint8_t> payload;
  uint32_t checksum = 0;
};

// ---- Event info for SHOW BINLOG EVENTS -------------------------------------

struct BinlogEventInfo {
  std::string log_name;
  uint64_t position = 0;
  std::string event_type;
  uint32_t server_id = 1;
  uint64_t end_log_pos = 0;
  std::string info;
};

// ---- Writer ----------------------------------------------------------------

class BinlogWriter {
 public:
  BinlogWriter() = default;
  ~BinlogWriter();

  void Initialize(const std::string& file_prefix, size_t max_size_mb = 1024);
  void Shutdown();

  void WriteEvent(BinlogEventType type, uint64_t txn_id,
                  const std::vector<uint8_t>& payload);
  void WriteQueryEvent(uint64_t txn_id, const std::string& sql);
  void WriteRowEvent(BinlogEventType type, uint64_t txn_id,
                     const std::string& table_name,
                     const std::vector<std::string>& columns,
                     const std::vector<std::string>& values);

  void Flush();
  void Rotate();

  // Returns (file_index, byte_offset)
  std::pair<int, uint64_t> GetPosition() const;
  bool IsInitialized() const { return initialized_; }

  // ---- Log management -------------------------------------------------------
  /** Get the current binlog file name (e.g., "goods_db_binlog.000001") */
  std::string GetCurrentFileName() const;

  /** Get the current byte position in the current binlog file */
  uint64_t GetCurrentPosition() const;

  /**
   * Read binlog events for SHOW BINLOG EVENTS command.
   * @param log_name Specific log file name, or empty for current
   * @param from_pos Starting byte position
   * @param limit Max number of events (-1 = all)
   * @return List of event info structs
   */
  std::vector<BinlogEventInfo> ReadEvents(const std::string& log_name,
                                          uint64_t from_pos,
                                          int64_t limit) const;

  /** Purge binlog files older than (before) the specified log file */
  void PurgeTo(const std::string& log_name);

  /** Reset all binlog files (RESET MASTER) */
  void Reset();

  /** Get the list of all binlog file names */
  std::vector<std::string> ListBinlogFiles() const;

 private:
  std::ofstream file_;
  std::mutex mutex_;
  std::string file_prefix_;
  size_t max_size_bytes_ = 1024ULL * 1024 * 1024;
  size_t current_size_ = 0;
  int file_index_ = 1;
  uint64_t current_pos_ = 0;
  bool initialized_ = false;

  std::string GetFilePath(int index) const;
  std::string GetIndexFilePath() const;
  void WriteUint32(uint32_t val);
  void WriteUint64(uint64_t val);
  void WriteString(const std::string& s);
  uint32_t CalculateCRC32(const std::vector<uint8_t>& data);
  void UpdateIndexFile();
};

// ---- Reader ----------------------------------------------------------------

class BinlogReader {
 public:
  explicit BinlogReader(const std::string& file_path);
  ~BinlogReader();

  bool ReadNextEvent(BinlogEvent& event);
  bool IsOpen() const { return file_.is_open(); }
  void SeekToPosition(uint64_t position);

 private:
  std::ifstream file_;
};

// ---- Event helpers ---------------------------------------------------------

const char* BinlogEventTypeToString(BinlogEventType type);

}  // namespace goods_db
