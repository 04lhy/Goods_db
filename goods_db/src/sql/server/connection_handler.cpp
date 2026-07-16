#include "sql/server/connection_handler.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>

#include "sql/executor/execution_engine.h"
#include "sql/goods_handler.h"
#include "sql/log/log_manager.h"
#include "sql/network/connection.h"
#include "sql/protocol/protocol_text.h"
#include "sql/security/auth_manager.h"
#include "storage/table/tuple.h"
#include "type/value.h"

namespace goods_db {

// ---- Global connection registry ---------------------------------------------

static std::map<uint32_t, ConnectionHandler*> g_connection_registry;
static std::mutex g_registry_mutex;
std::atomic<uint32_t> ConnectionHandler::next_conn_id_{1};

ConnectionHandler::RegistryMap& ConnectionHandler::GetRegistry() {
  return g_connection_registry;
}
std::mutex& ConnectionHandler::GetRegistryMutex() {
  return g_registry_mutex;
}

ConnectionHandler::ConnectionHandler(int client_fd,
                                     const std::string& remote_addr,
                                     uint16_t remote_port,
                                     goods_handler* engine,
                                     AuthManager* auth_mgr,
                                     LogManager* log_mgr,
                                     LockManager* lock_mgr,
                                     ExecutionEngine* exec_engine)
    : engine_(engine),
      auth_mgr_(auth_mgr),
      log_mgr_(log_mgr),
      lock_mgr_(lock_mgr),
      exec_engine_(exec_engine) {
  connection_ = std::make_unique<Connection>(client_fd, remote_addr,
                                              remote_port);
  // Register in global registry for SHOW PROCESSLIST / KILL
  conn_id_ = next_conn_id_.fetch_add(1);
  {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    g_connection_registry[conn_id_] = this;
  }
}

ConnectionHandler::~ConnectionHandler() {
  // Deregister from global registry
  {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    g_connection_registry.erase(conn_id_);
  }
  if (connection_) {
    connection_->Close();
  }
}

void ConnectionHandler::Stop() {
  running_ = false;
  if (connection_) connection_->Close();
}

void ConnectionHandler::Run() {
  auto proto = std::make_unique<ProtocolText>();
  proto->SetWriteFd(connection_->GetFd());
  connection_->SetProtocol(std::move(proto));

  if (!SendHandshake()) return;
  if (!HandleAuth()) return;

  while (running_ && !connection_->IsClosed()) {
    std::string payload;
    if (!connection_->ReadPacket(payload)) break;
    if (payload.empty()) continue;

    std::string cmd(payload);

    if (cmd == "PING" || cmd == "\x0E") {
      HandlePing();
    } else if (cmd == "QUIT" || cmd == "\x01") {
      break;
    } else if (cmd.rfind("QUERY", 0) == 0) {
      size_t pos = cmd.find('\0');
      if (pos != std::string::npos && pos + 1 < cmd.size()) {
        HandleQuery(cmd.substr(pos + 1));
      } else if (cmd.size() > 6) {
        HandleQuery(cmd.substr(6));
      }
    } else {
      // Raw SQL
      HandleQuery(cmd);
    }
  }

  if (log_mgr_) {
    log_mgr_->GetErrorLog().Log(
        ErrorLog::INFO, __FILE__, __LINE__,
        "Connection closed: %s@%s:%d",
        connection_->GetUser().c_str(),
        connection_->GetRemoteAddr().c_str(),
        connection_->GetRemotePort());
  }
}

bool ConnectionHandler::SendHandshake() {
  connection_->SetState(Connection::State::AUTH);
  return connection_->WritePacketStr("goods_db 0.1.0\n");
}

bool ConnectionHandler::HandleAuth() {
  std::string payload;
  if (!connection_->ReadPacket(payload)) return false;

  std::string user, password;
  // Parse: "AUTH\0user\0password\0db\0" or "user\0password\0..."
  size_t p1 = payload.find('\0');
  if (p1 == std::string::npos) {
    connection_->GetProtocol()->SendError(1045, "28000", "Invalid auth format");
    connection_->GetProtocol()->Flush();
    return false;
  }

  if (payload.rfind("AUTH", 0) == 0) {
    // Skip "AUTH\0"
    size_t start = p1 + 1;
    size_t p2 = payload.find('\0', start);
    if (p2 != std::string::npos) {
      user = payload.substr(start, p2 - start);
      size_t p3 = payload.find('\0', p2 + 1);
      if (p3 != std::string::npos) {
        password = payload.substr(p2 + 1, p3 - p2 - 1);
      }
    }
  } else {
    user = payload.substr(0, p1);
    size_t p2 = payload.find('\0', p1 + 1);
    if (p2 != std::string::npos) {
      password = payload.substr(p1 + 1, p2 - p1 - 1);
    }
  }

  bool ok = false;
  if (auth_mgr_) {
    ok = auth_mgr_->CheckConnection(connection_->GetRemoteAddr(), user, password);
  } else {
    ok = true;
  }

  if (ok) {
    connection_->SetAuthenticated(true, user);
    connection_->SetState(Connection::State::READY);
    connection_->GetProtocol()->SendOk(0, 0, "Authentication successful");
    connection_->GetProtocol()->Flush();
    return true;
  } else {
    connection_->GetProtocol()->SendError(1045, "28000",
        "Access denied for user '" + user + "'");
    connection_->GetProtocol()->Flush();
    if (auth_mgr_) auth_mgr_->RecordAuthFailure(connection_->GetRemoteAddr());
    connection_->Close();
    return false;
  }
}

bool ConnectionHandler::HandleQuery(const std::string& sql) {
  connection_->SetState(Connection::State::QUERYING);
  current_query_ = sql;  // expose for SHOW PROCESSLIST

  // Log the query for debugging
  if (log_mgr_) {
    log_mgr_->GetErrorLog().Log(ErrorLog::INFO, __FILE__, __LINE__,
                                "SQL: %s", sql.c_str());
  }

  if (log_mgr_) {
    log_mgr_->GetQueryLog().LogQuery(
        connection_->GetUser(), connection_->GetRemoteAddr(),
        connection_->GetDatabase(), sql, 0, 0);
  }

  auto* proto = connection_->GetProtocol();
  if (!proto) return false;

  if (!exec_engine_) {
    proto->SendError(5000, "HY000", "No execution engine available");
    proto->Flush();
    connection_->SetState(Connection::State::READY);
    return true;
  }

  // ---- Set current user context for permission enforcement ---------------
  exec_engine_->SetCurrentUser(connection_->GetUser(),
                                connection_->GetRemoteAddr());

  // ---- Execute SQL via ExecutionEngine -----------------------------------
  try {
    std::vector<Tuple> results;
    const Schema* output_schema = nullptr;
    int rc = exec_engine_->ExecuteSQL(sql, &results, &output_schema);

    if (rc != 0) {
      std::string err = exec_engine_->GetLastError();
      proto->SendError(1064, "42000", err.empty() ? "Execution failed" : err);
      proto->Flush();
      connection_->SetState(Connection::State::READY);
      return true;
    }

    // ---- Serialize results ----------------------------------------------
    if (output_schema != nullptr && output_schema->GetColumnCount() > 0) {
    uint32_t col_count = output_schema->GetColumnCount();
    proto->StartResultMetadata(col_count);

    for (uint32_t i = 0; i < col_count; i++) {
      const Column& col = output_schema->GetColumn(i);
      std::string type_str = TypeIdToString(col.column_type);
      proto->SendColumnDefinition(col.column_name, type_str, col.max_length);
    }
    proto->EndResultMetadata();

    for (const auto& tuple : results) {
      proto->StartRow();
      for (uint32_t i = 0; i < col_count; i++) {
        const Value& val = tuple.GetValue(output_schema, i);
        if (val.GetTypeId() == TypeId::INVALID) {
          proto->StoreNull();
        } else {
          switch (val.GetTypeId()) {
            case TypeId::BOOLEAN:
              proto->StoreInteger(val.GetAsBoolean() ? 1 : 0);
              break;
            case TypeId::TINYINT:
              proto->StoreInteger(val.GetAsTinyInt());
              break;
            case TypeId::SMALLINT:
              proto->StoreInteger(val.GetAsSmallInt());
              break;
            case TypeId::INTEGER:
              proto->StoreInteger(val.GetAsInteger());
              break;
            case TypeId::BIGINT:
              proto->StoreInteger(val.GetAsBigInt());
              break;
            case TypeId::DECIMAL:
              proto->StoreFloat(val.GetAsDecimal());
              break;
            case TypeId::TIMESTAMP: {
              std::string ts_str = Value::FormatTimestamp(val.GetAsTimestamp());
              proto->StoreString(ts_str.c_str(), ts_str.size());
              break;
            }
            case TypeId::VARCHAR: {
              const std::string& s = val.GetAsVarchar();
              proto->StoreString(s.c_str(), s.size());
              break;
            }
            default: {
              std::string s = val.ToString();
              proto->StoreString(s.c_str(), s.size());
              break;
            }
          }
        }
      }
      proto->EndRow();
    }
    proto->SendEOF();
  } else {
    int affected = exec_engine_->GetAffectedRows();
    proto->SendOk(affected > 0 ? affected : results.size(), 0, "");
  }

  proto->Flush();
  } catch (const std::exception& e) {
    proto->SendError(5000, "HY000", std::string("Internal error: ") + e.what());
    proto->Flush();
  }
  current_query_.clear();
  connection_->SetState(Connection::State::READY);
  return true;
}

bool ConnectionHandler::HandlePing() {
  connection_->GetProtocol()->SendOk(0, 0, "pong");
  connection_->GetProtocol()->Flush();
  return true;
}

std::vector<std::string> ConnectionHandler::SplitSql(const std::string& sql) {
  std::vector<std::string> statements;
  std::string current;
  bool in_string = false;
  char string_char = '\0';

  for (size_t i = 0; i < sql.size(); i++) {
    char c = sql[i];
    if (in_string) {
      current += c;
      if (c == string_char && (i == 0 || sql[i - 1] != '\\')) in_string = false;
    } else if (c == '\'' || c == '"') {
      in_string = true;
      string_char = c;
      current += c;
    } else if (c == ';') {
      auto s = current.find_first_not_of(" \t\n\r");
      auto e = current.find_last_not_of(" \t\n\r");
      if (s != std::string::npos)
        statements.push_back(current.substr(s, e - s + 1));
      current.clear();
    } else {
      current += c;
    }
  }
  auto s = current.find_first_not_of(" \t\n\r");
  auto e = current.find_last_not_of(" \t\n\r");
  if (s != std::string::npos)
    statements.push_back(current.substr(s, e - s + 1));
  return statements;
}

}  // namespace goods_db
