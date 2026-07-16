#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "network/goods_db_protocol.h"

namespace goods_db {
namespace studio {

// =============================================================================
// ApiHandler — manages goods_db server connections and SQL execution
//
// Thread-safe. Maintains a persistent connection to the goods_db server.
// Each HTTP request creates a temporary worker for the TCP I/O.
// =============================================================================
class ApiHandler {
 public:
  ApiHandler();
  ~ApiHandler();

  // ---- Connection management ------------------------------------------------
  struct ConnectRequest {
    std::string host;
    uint16_t port = 3307;
    std::string user;
    std::string password;
    std::string database;
  };

  struct ApiResponse {
    bool success = false;
    std::string error;
    std::string message;
  };

  ApiResponse Connect(const ConnectRequest& req);
  ApiResponse Disconnect();
  bool IsConnected();

  // ---- SQL Execution --------------------------------------------------------
  struct ColumnDef {
    std::string name;
    std::string type_name;
    int length = 0;
  };

  struct ExecuteResponse {
    bool success = false;
    std::string error;
    std::vector<ColumnDef> columns;
    std::vector<std::vector<std::string>> rows;
    uint64_t affected_rows = 0;
    uint64_t last_insert_id = 0;
    uint64_t exec_time_ms = 0;
  };

  ExecuteResponse Execute(const std::string& sql);

  // ---- Schema discovery -----------------------------------------------------
  struct TableInfo {
    std::string name;
  };

  struct ColumnInfo {
    std::string name;
    std::string type_name;
    int length = 0;
    bool nullable = true;
    bool is_primary_key = false;
  };

  std::vector<std::string> GetDatabases();
  std::vector<TableInfo> GetTables(const std::string& db);
  std::vector<ColumnInfo> GetColumns(const std::string& db,
                                      const std::string& table);

  // Serialize response to JSON (simple, dependency-free)
  static std::string ToJson(const ExecuteResponse& resp);
  static std::string ToJson(const ApiResponse& resp);
  static std::string ToJson(const std::vector<std::string>& items);
  static std::string ToJson(const std::vector<TableInfo>& tables);
  static std::string ToJson(const std::vector<ColumnInfo>& columns);
  static std::string JsonEscape(const std::string& s);

 private:
  mutable std::mutex mutex_;
  std::unique_ptr<GoodsDbProtocol> client_;
  bool connected_ = false;
};

}  // namespace studio
}  // namespace goods_db
