#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace goods_db {
namespace studio {

// =============================================================================
// Qt-free protocol structures and client for goods_db server
//
// Uses raw POSIX sockets instead of QTcpSocket, std::string instead of
// QString. Identical protocol logic to DatabaseWorker but without Qt.
// =============================================================================

struct ProtoColumnInfo {
  std::string name;
  std::string type_name;
  int length = 0;
};

struct ProtoQueryResult {
  std::vector<ProtoColumnInfo> columns;
  std::vector<std::vector<std::string>> rows;
  uint64_t affected_rows = 0;
  uint64_t last_insert_id = 0;
  bool is_error = false;
  std::string error_message;
  uint64_t exec_time_ms = 0;
};

// =============================================================================
// GoodsDbProtocol — Qt-free TCP client for goods_db server protocol
//
// All methods are synchronous and blocking. Call from any thread.
// =============================================================================
class GoodsDbProtocol {
 public:
  GoodsDbProtocol();
  ~GoodsDbProtocol();

  // ---- Connection -----------------------------------------------------------
  bool Connect(const std::string& host, uint16_t port, int timeout_ms = 5000);
  void Disconnect();
  bool IsConnected() const;

  // ---- Authentication -------------------------------------------------------
  bool Authenticate(const std::string& user, const std::string& password,
                    const std::string& db = "");

  // ---- Commands -------------------------------------------------------------
  bool Ping();
  ProtoQueryResult Execute(const std::string& sql);

  // ---- Error ----------------------------------------------------------------
  std::string GetLastError() const { return last_error_; }

 private:
  int sock_fd_ = -1;
  std::string last_error_;

  // Packet I/O
  bool WritePacket(const std::string& data);
  bool ReadPacket(std::string& payload, int timeout_ms = 5000);

  // Low-level I/O
  bool WriteAll(const char* data, size_t len);
  bool ReadExact(char* buf, size_t count, int timeout_ms);
  bool WaitReadable(int timeout_ms);

  // Response parsing
  ProtoQueryResult ParseResponse(const std::string& data);
  ProtoQueryResult ParseResultSet(const std::string& data);
  ProtoQueryResult ParseOkError(const std::string& line);

  void SetError(const std::string& err) { last_error_ = err; }
};

}  // namespace studio
}  // namespace goods_db
