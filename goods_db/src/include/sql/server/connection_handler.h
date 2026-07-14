#pragma once

#include <atomic>
#include <memory>
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

 private:
  std::unique_ptr<Connection> connection_;
  class goods_handler* engine_;
  AuthManager* auth_mgr_;
  LogManager* log_mgr_;
  LockManager* lock_mgr_;
  ExecutionEngine* exec_engine_;
  std::atomic<bool> running_{true};

  bool SendHandshake();
  bool HandleAuth();
  bool HandleQuery(const std::string& sql);
  bool HandlePing();

  static std::vector<std::string> SplitSql(const std::string& sql);
};

}  // namespace goods_db
