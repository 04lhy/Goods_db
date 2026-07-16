#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "sql/network/connection.h"

namespace goods_db {

// Forward declarations
class goods_handler;
class AuthManager;
class LogManager;
class LockManager;
class ExecutionEngine;

// =============================================================================
// ConnectionHandler — manages the full lifecycle of one client connection
//
// Each handler registers itself in a global registry on construction and
// deregisters on destruction, enabling SHOW PROCESSLIST and KILL commands.
// =============================================================================
class ConnectionHandler {
 public:
  ConnectionHandler(int client_fd, const std::string& remote_addr,
                    uint16_t remote_port, class goods_handler* engine,
                    AuthManager* auth_mgr, LogManager* log_mgr,
                    LockManager* lock_mgr, ExecutionEngine* exec_engine);
  ~ConnectionHandler();

  void Run();
  void Stop();

  // ---- Accessors for SHOW PROCESSLIST ---------------------------------------
  uint32_t GetConnectionId() const { return conn_id_; }
  Connection* GetConnection() const { return connection_.get(); }
  const std::string& GetCurrentQuery() const { return current_query_; }

  // ---- Global connection registry -------------------------------------------
  using RegistryMap = std::map<uint32_t, ConnectionHandler*>;
  static RegistryMap& GetRegistry();
  static std::mutex& GetRegistryMutex();

 private:
  std::unique_ptr<Connection> connection_;
  class goods_handler* engine_;
  AuthManager* auth_mgr_;
  LogManager* log_mgr_;
  LockManager* lock_mgr_;
  ExecutionEngine* exec_engine_;
  std::atomic<bool> running_{true};

  static std::atomic<uint32_t> next_conn_id_;
  uint32_t conn_id_ = 0;
  std::string current_query_;  // currently executing SQL, for SHOW PROCESSLIST

  bool SendHandshake();
  bool HandleAuth();
  bool HandleQuery(const std::string& sql);
  bool HandlePing();

  static std::vector<std::string> SplitSql(const std::string& sql);
};

}  // namespace goods_db
