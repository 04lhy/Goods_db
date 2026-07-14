#pragma once

#include <string>

#include "sql/protocol/protocol.h"

namespace goods_db {

// =============================================================================
// ProtocolText — text-based wire protocol
//
// Uses human-readable text lines for easy debugging (telnet/nc compatible).
//
// Wire format (simplified MySQL-like text protocol):
//   ColumnCount:  "Columns: N\n"
//   ColumnDef:    "ColDef: name type len\n"  (repeated N times)
//   RowData:      "Row: val1\tval2\t...\n"   (\N for NULL)
//   EOF:          "EOF\n"
//   OK:           "OK affected_rows last_insert_id info\n"
//   ERR:          "ERR error_code sql_state message\n"
// =============================================================================
class ProtocolText : public Protocol {
 public:
  ProtocolText() = default;
  ~ProtocolText() override = default;

  void SetWriteFd(int fd) override;

  void StartResultMetadata(uint32_t num_columns) override;
  void SendColumnDefinition(const std::string& name,
                            const std::string& type_name,
                            uint32_t col_length) override;
  void EndResultMetadata() override;

  void StartRow() override;
  void StoreNull() override;
  void StoreInteger(int64_t value) override;
  void StoreFloat(double value) override;
  void StoreString(const char* str, size_t length) override;
  void EndRow() override;

  void SendOk(uint64_t affected_rows, uint64_t last_insert_id,
              const std::string& info = "") override;
  void SendError(uint16_t error_code, const std::string& sql_state,
                 const std::string& message) override;
  void SendEOF() override;

  void Flush() override;

 private:
  int fd_ = -1;
  std::string buffer_;
  uint8_t sequence_id_ = 0;
  uint32_t num_columns_ = 0;
  uint32_t column_count_ = 0;
  bool in_row_ = false;
  bool first_in_row_ = true;

  void ResetSequence() { sequence_id_ = 0; }
};

}  // namespace goods_db
