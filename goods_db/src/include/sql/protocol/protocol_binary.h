#pragma once

#include <string>
#include <vector>

#include "sql/protocol/protocol.h"

namespace goods_db {

// =============================================================================
// ProtocolBinary — binary wire protocol
//
// Compact binary encoding for prepared statements and high-performance use.
//
// Wire format:
//   ColumnCount:  4 bytes uint32 LE
//   ColumnDef:    2-byte LE len + name, 2-byte LE len + type
//   Row value:    1-byte type tag (0=null, 1=int64, 2=float64, 3=string)
//                 + data (int64: 8 bytes LE, float64: 8 bytes LE,
//                         string: 2 bytes LE len + data)
//   OK:           1 byte 0x00 + varint affected_rows + varint last_insert_id
//   ERR:          1 byte 0xFF + 2 bytes LE errcode + 5 bytes sqlstate + string msg
//   EOF:          1 byte 0xFE
// =============================================================================
class ProtocolBinary : public Protocol {
 public:
  ProtocolBinary() = default;
  ~ProtocolBinary() override = default;

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
  std::vector<uint8_t> buffer_;
  uint8_t sequence_id_ = 0;
  uint32_t num_columns_ = 0;
  uint32_t column_count_ = 0;
  bool in_row_ = false;
  uint32_t row_value_count_ = 0;

  void WritePacket(const uint8_t* data, size_t len);
  void WriteUint8(uint8_t val);
  void WriteUint16(uint16_t val);
  void WriteUint32(uint32_t val);
  void WriteUint64(uint64_t val);
  void WriteString(const std::string& s);
  void WriteLenEncoded(uint64_t val);
  void ResetSequence() { sequence_id_ = 0; }
};

}  // namespace goods_db
