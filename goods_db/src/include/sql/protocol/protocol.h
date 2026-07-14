#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace goods_db {

// =============================================================================
// Packet format constants
// =============================================================================
constexpr uint32_t kMaxPayloadLength = 16 * 1024 * 1024;  // 16 MB
constexpr uint8_t kOkPacketMarker = 0x00;
constexpr uint8_t kErrPacketMarker = 0xFF;
constexpr uint8_t kEofPacketMarker = 0xFE;

// =============================================================================
// PacketHeader: 4-byte fixed header for every packet
//   - payload_length: 3 bytes, little-endian
//   - sequence_id:    1 byte, incrementing per packet
// =============================================================================
struct PacketHeader {
  uint32_t payload_length;
  uint8_t sequence_id;
};

uint32_t ReadPacketLength(const uint8_t* header);
void WritePacketLength(uint8_t* header, uint32_t len);

// =============================================================================
// ColumnMeta: column metadata for result set
// =============================================================================
struct ColumnMeta {
  std::string name;
  std::string type_name;
  uint32_t col_length = 0;
};

// =============================================================================
// Protocol — abstract base class
//
// Reference: MySQL sql/protocol.h. Provides a unified interface for serializing
// query results and server response packets over the network. Subclasses
// implement text (human-readable) and binary (compact) wire formats.
// =============================================================================
class Protocol {
 public:
  Protocol() = default;
  virtual ~Protocol() = default;

  // Set the output file descriptor for network I/O
  virtual void SetWriteFd(int fd) = 0;

  // ---- Result set metadata ------------------------------------------------
  virtual void StartResultMetadata(uint32_t num_columns) = 0;
  virtual void SendColumnDefinition(const std::string& name,
                                    const std::string& type_name,
                                    uint32_t col_length) = 0;
  virtual void EndResultMetadata() = 0;

  // ---- Row data -----------------------------------------------------------
  virtual void StartRow() = 0;
  virtual void StoreNull() = 0;
  virtual void StoreInteger(int64_t value) = 0;
  virtual void StoreFloat(double value) = 0;
  virtual void StoreString(const char* str, size_t length) = 0;
  virtual void EndRow() = 0;

  // ---- Response packets ---------------------------------------------------
  virtual void SendOk(uint64_t affected_rows, uint64_t last_insert_id,
                      const std::string& info = "") = 0;
  virtual void SendError(uint16_t error_code, const std::string& sql_state,
                         const std::string& message) = 0;
  virtual void SendEOF() = 0;

  // ---- Flush --------------------------------------------------------------
  virtual void Flush() = 0;
};

}  // namespace goods_db
