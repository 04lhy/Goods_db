#include "sql/protocol/protocol_binary.h"

#include <cstring>
#include <unistd.h>

namespace goods_db {

// ---- Helpers ---------------------------------------------------------------

void ProtocolBinary::WriteUint8(uint8_t val) {
  buffer_.push_back(val);
}

void ProtocolBinary::WriteUint16(uint16_t val) {
  buffer_.push_back(static_cast<uint8_t>(val & 0xFF));
  buffer_.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
}

void ProtocolBinary::WriteUint32(uint32_t val) {
  buffer_.push_back(static_cast<uint8_t>(val & 0xFF));
  buffer_.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
  buffer_.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
  buffer_.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

void ProtocolBinary::WriteUint64(uint64_t val) {
  for (int i = 0; i < 8; i++) {
    buffer_.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
  }
}

void ProtocolBinary::WriteString(const std::string& s) {
  WriteUint16(static_cast<uint16_t>(s.size()));
  buffer_.insert(buffer_.end(), s.begin(), s.end());
}

void ProtocolBinary::WriteLenEncoded(uint64_t val) {
  if (val < 251) {
    buffer_.push_back(static_cast<uint8_t>(val));
  } else if (val < 65536) {
    buffer_.push_back(0xFC);
    WriteUint16(static_cast<uint16_t>(val));
  } else if (val < 16777216) {
    buffer_.push_back(0xFD);
    buffer_.push_back(static_cast<uint8_t>(val & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
  } else {
    buffer_.push_back(0xFE);
    WriteUint64(val);
  }
}

void ProtocolBinary::WritePacket(const uint8_t* data, size_t len) {
  // 4-byte header
  uint8_t header[4];
  WritePacketLength(header, static_cast<uint32_t>(len));
  header[3] = sequence_id_++;
  buffer_.insert(buffer_.end(), header, header + 4);
  buffer_.insert(buffer_.end(), data, data + len);
}

// ---- Protocol interface ----------------------------------------------------

void ProtocolBinary::SetWriteFd(int fd) {
  fd_ = fd;
  ResetSequence();
}

void ProtocolBinary::StartResultMetadata(uint32_t num_columns) {
  num_columns_ = num_columns;
  column_count_ = 0;
  buffer_.clear();
  ResetSequence();
  WriteUint32(num_columns);
}

void ProtocolBinary::SendColumnDefinition(const std::string& name,
                                           const std::string& type_name,
                                           uint32_t /*col_length*/) {
  WriteString(name);
  WriteString(type_name);
  column_count_++;
}

void ProtocolBinary::EndResultMetadata() {
  // Ready for row data
}

void ProtocolBinary::StartRow() {
  in_row_ = true;
  row_value_count_ = 0;
}

void ProtocolBinary::StoreNull() {
  WriteUint8(0);  // type tag: null
  row_value_count_++;
}

void ProtocolBinary::StoreInteger(int64_t value) {
  WriteUint8(1);  // type tag: int64
  WriteUint64(static_cast<uint64_t>(value));
  row_value_count_++;
}

void ProtocolBinary::StoreFloat(double value) {
  WriteUint8(2);  // type tag: float64
  uint64_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  WriteUint64(bits);
  row_value_count_++;
}

void ProtocolBinary::StoreString(const char* str, size_t length) {
  WriteUint8(3);  // type tag: string
  WriteUint16(static_cast<uint16_t>(length));
  buffer_.insert(buffer_.end(),
                 reinterpret_cast<const uint8_t*>(str),
                 reinterpret_cast<const uint8_t*>(str) + length);
  row_value_count_++;
}

void ProtocolBinary::EndRow() {
  in_row_ = false;
  (void)row_value_count_;
}

void ProtocolBinary::SendOk(uint64_t affected_rows, uint64_t last_insert_id,
                             const std::string& info) {
  ResetSequence();
  std::vector<uint8_t> payload;
  payload.push_back(kOkPacketMarker);
  // Write affected_rows as length-encoded
  auto write_lenenc = [&](uint64_t val) {
    if (val < 251) {
      payload.push_back(static_cast<uint8_t>(val));
    } else if (val < 65536) {
      payload.push_back(0xFC);
      payload.push_back(static_cast<uint8_t>(val & 0xFF));
      payload.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    } else {
      payload.push_back(0xFE);
      for (int i = 0; i < 8; i++) {
        payload.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
      }
    }
  };
  write_lenenc(affected_rows);
  write_lenenc(last_insert_id);
  // Server status + warnings (both 0 for now)
  payload.push_back(0);
  payload.push_back(0);
  payload.push_back(0);
  payload.push_back(0);
  if (!info.empty()) {
    payload.insert(payload.end(), info.begin(), info.end());
  }
  WritePacket(payload.data(), payload.size());
}

void ProtocolBinary::SendError(uint16_t error_code,
                                const std::string& sql_state,
                                const std::string& message) {
  ResetSequence();
  std::vector<uint8_t> payload;
  payload.push_back(kErrPacketMarker);
  WriteUint16(error_code);
  // SQL state marker + 5 bytes
  payload.push_back('#');
  for (size_t i = 0; i < 5 && i < sql_state.size(); i++) {
    payload.push_back(static_cast<uint8_t>(sql_state[i]));
  }
  while (payload.size() < 9) payload.push_back(' ');
  payload.insert(payload.end(), message.begin(), message.end());
  WritePacket(payload.data(), payload.size());
}

void ProtocolBinary::SendEOF() {
  ResetSequence();
  uint8_t payload = kEofPacketMarker;
  WritePacket(&payload, 1);
}

void ProtocolBinary::Flush() {
  if (fd_ < 0 || buffer_.empty()) {
    return;
  }
  ssize_t written = write(fd_, buffer_.data(), buffer_.size());
  if (written < 0) {
    // Error — caller should handle
  }
  buffer_.clear();
}

}  // namespace goods_db
