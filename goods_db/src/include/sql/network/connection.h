#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "sql/protocol/protocol.h"

namespace goods_db {

// =============================================================================
// Connection — wraps a TCP client socket and owns a Protocol instance
//
// Lifecycle states: INIT → AUTH → READY → QUERYING → SENDING → READY
//   INIT:     fresh connection, handshake not yet done
//   AUTH:     waiting for authentication packet
//   READY:    authenticated, waiting for commands
//   QUERYING: executing a SQL query
//   SENDING:  sending results back to client
//   CLOSING:  connection is shutting down
//
// Network statistics (bytes sent/received) are tracked per-connection and
// also exposed at the server level via static counters.
// =============================================================================
class Connection {
 public:
  enum class State { INIT, AUTH, READY, QUERYING, SENDING, CLOSING };

  Connection(int client_fd, const std::string& remote_addr,
             uint16_t remote_port);
  ~Connection();

  // Non-copyable, non-movable (std::atomic members prevent default move)
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  Connection(Connection&&) = delete;
  Connection& operator=(Connection&&) = delete;

  // ---- Socket I/O ---------------------------------------------------------
  int GetFd() const { return fd_; }
  const std::string& GetRemoteAddr() const { return remote_addr_; }
  uint16_t GetRemotePort() const { return remote_port_; }

  // Read a complete packet from the socket.
  // Returns false on connection error or EOF.
  bool ReadPacket(std::string& payload);

  // Write raw data with packet framing.
  bool WritePacket(const uint8_t* data, size_t len);
  bool WritePacketStr(const std::string& data);

  // ---- Network statistics --------------------------------------------------
  uint64_t GetBytesSent() const { return bytes_sent_.load(); }
  uint64_t GetBytesReceived() const { return bytes_received_.load(); }

  // Global server-level network statistics
  static uint64_t GetTotalBytesSent();
  static uint64_t GetTotalBytesReceived();
  static uint64_t GetTotalConnections();
  static uint64_t GetActiveConnectionCount();

  // ---- State management ---------------------------------------------------
  State GetState() const { return state_; }
  void SetState(State s) { state_ = s; }

  bool IsClosed() const { return fd_ < 0; }

  // ---- Authentication -----------------------------------------------------
  bool IsAuthenticated() const { return authenticated_; }
  void SetAuthenticated(bool auth, const std::string& user = "",
                        const std::string& db = "");
  const std::string& GetUser() const { return user_; }
  const std::string& GetDatabase() const { return db_; }
  void SetDatabase(const std::string& db) { db_ = db; }

  // ---- Protocol -----------------------------------------------------------
  Protocol* GetProtocol() { return protocol_.get(); }
  void SetProtocol(std::unique_ptr<Protocol> proto) {
    protocol_ = std::move(proto);
  }

  // ---- Close --------------------------------------------------------------
  void Close();

 private:
  int fd_;
  std::string remote_addr_;
  uint16_t remote_port_;
  State state_ = State::INIT;
  bool authenticated_ = false;
  std::string user_;
  std::string db_;
  std::unique_ptr<Protocol> protocol_;

  // Per-connection statistics
  std::atomic<uint64_t> bytes_sent_{0};
  std::atomic<uint64_t> bytes_received_{0};

  // Server-level statistics (shared across all connections)
  static std::atomic<uint64_t> total_bytes_sent_;
  static std::atomic<uint64_t> total_bytes_received_;
  static std::atomic<uint64_t> total_connections_;
  static std::atomic<uint64_t> active_connections_;
};

}  // namespace goods_db
