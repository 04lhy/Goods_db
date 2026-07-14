#include "sql/protocol/protocol_text.h"

#include <cstring>
#include <unistd.h>

namespace goods_db {

void ProtocolText::SetWriteFd(int fd) {
  fd_ = fd;
  ResetSequence();
}

// ---- Result set metadata ---------------------------------------------------

void ProtocolText::StartResultMetadata(uint32_t num_columns) {
  num_columns_ = num_columns;
  column_count_ = 0;
  buffer_.clear();
  ResetSequence();
  buffer_ += "Columns: " + std::to_string(num_columns) + "\n";
}

void ProtocolText::SendColumnDefinition(const std::string& name,
                                         const std::string& type_name,
                                         uint32_t col_length) {
  buffer_ += "ColDef: " + name + " " + type_name + " " +
             std::to_string(col_length) + "\n";
  column_count_++;
}

void ProtocolText::EndResultMetadata() {
  buffer_ += "#ROWS\n";
}

// ---- Row data --------------------------------------------------------------

void ProtocolText::StartRow() {
  in_row_ = true;
  first_in_row_ = true;
  buffer_ += "Row: ";
}

void ProtocolText::StoreNull() {
  if (!first_in_row_) buffer_ += "\t";
  buffer_ += "\\N";
  first_in_row_ = false;
}

void ProtocolText::StoreInteger(int64_t value) {
  if (!first_in_row_) buffer_ += "\t";
  buffer_ += std::to_string(value);
  first_in_row_ = false;
}

void ProtocolText::StoreFloat(double value) {
  if (!first_in_row_) buffer_ += "\t";
  buffer_ += std::to_string(value);
  first_in_row_ = false;
}

void ProtocolText::StoreString(const char* str, size_t length) {
  if (!first_in_row_) buffer_ += "\t";
  for (size_t i = 0; i < length; i++) {
    if (str[i] == '\t') buffer_ += "\\t";
    else if (str[i] == '\n') buffer_ += "\\n";
    else if (str[i] == '\\') buffer_ += "\\\\";
    else buffer_ += str[i];
  }
  first_in_row_ = false;
}

void ProtocolText::EndRow() {
  buffer_ += "\n";
  in_row_ = false;
}

// ---- Response packets ------------------------------------------------------

void ProtocolText::SendOk(uint64_t affected_rows, uint64_t last_insert_id,
                           const std::string& info) {
  ResetSequence();
  buffer_.clear();
  buffer_ += "OK " + std::to_string(affected_rows) + " " +
             std::to_string(last_insert_id);
  if (!info.empty()) buffer_ += " " + info;
  buffer_ += "\n";
}

void ProtocolText::SendError(uint16_t error_code,
                              const std::string& sql_state,
                              const std::string& message) {
  ResetSequence();
  buffer_.clear();
  buffer_ += "ERR " + std::to_string(error_code) + " " + sql_state + " " +
             message + "\n";
}

void ProtocolText::SendEOF() {
  buffer_ += "EOF\n";
}

// ---- Flush -----------------------------------------------------------------

void ProtocolText::Flush() {
  if (fd_ < 0 || buffer_.empty()) return;

  // Build packet: 4-byte header + payload
  uint32_t payload_len = static_cast<uint32_t>(buffer_.size());
  uint8_t header[4];
  WritePacketLength(header, payload_len);
  header[3] = sequence_id_++;

  // Write header + payload, retrying partial writes
  std::string packet(reinterpret_cast<const char*>(header), 4);
  packet += buffer_;
  buffer_.clear();

  size_t total = packet.size();
  size_t written = 0;
  while (written < total) {
    ssize_t n = write(fd_, packet.data() + written, total - written);
    if (n <= 0) break;
    written += static_cast<size_t>(n);
  }
}

}  // namespace goods_db
