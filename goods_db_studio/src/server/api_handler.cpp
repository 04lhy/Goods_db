#include "server/api_handler.h"

#include <algorithm>
#include <sstream>

namespace goods_db {
namespace studio {

// =============================================================================
// JSON helpers (lightweight — no external dependency)
// =============================================================================

std::string ApiHandler::JsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:   out += c;
    }
  }
  return out;
}

std::string ApiHandler::ToJson(const ApiResponse& resp) {
  std::ostringstream js;
  js << "{\"success\":" << (resp.success ? "true" : "false");
  if (!resp.error.empty())
    js << ",\"error\":\"" << JsonEscape(resp.error) << "\"";
  if (!resp.message.empty())
    js << ",\"message\":\"" << JsonEscape(resp.message) << "\"";
  js << "}";
  return js.str();
}

std::string ApiHandler::ToJson(const ExecuteResponse& resp) {
  std::ostringstream js;
  js << "{\"success\":" << (resp.success ? "true" : "false");
  if (!resp.error.empty())
    js << ",\"error\":\"" << JsonEscape(resp.error) << "\"";

  // Columns
  js << ",\"columns\":[";
  for (size_t i = 0; i < resp.columns.size(); i++) {
    if (i > 0) js << ",";
    js << "{\"name\":\"" << JsonEscape(resp.columns[i].name) << "\""
       << ",\"type_name\":\"" << JsonEscape(resp.columns[i].type_name) << "\""
       << ",\"length\":" << resp.columns[i].length << "}";
  }
  js << "]";

  // Rows
  js << ",\"rows\":[";
  for (size_t i = 0; i < resp.rows.size(); i++) {
    if (i > 0) js << ",";
    js << "[";
    for (size_t j = 0; j < resp.rows[i].size(); j++) {
      if (j > 0) js << ",";
      js << "\"" << JsonEscape(resp.rows[i][j]) << "\"";
    }
    js << "]";
  }
  js << "]";

  js << ",\"affected_rows\":" << resp.affected_rows
     << ",\"last_insert_id\":" << resp.last_insert_id
     << ",\"exec_time_ms\":" << resp.exec_time_ms
     << "}";
  return js.str();
}

std::string ApiHandler::ToJson(const std::vector<std::string>& items) {
  std::ostringstream js;
  js << "[";
  for (size_t i = 0; i < items.size(); i++) {
    if (i > 0) js << ",";
    js << "\"" << JsonEscape(items[i]) << "\"";
  }
  js << "]";
  return js.str();
}

std::string ApiHandler::ToJson(const std::vector<TableInfo>& tables) {
  std::ostringstream js;
  js << "[";
  for (size_t i = 0; i < tables.size(); i++) {
    if (i > 0) js << ",";
    js << "{\"name\":\"" << JsonEscape(tables[i].name) << "\"}";
  }
  js << "]";
  return js.str();
}

std::string ApiHandler::ToJson(const std::vector<ColumnInfo>& columns) {
  std::ostringstream js;
  js << "[";
  for (size_t i = 0; i < columns.size(); i++) {
    if (i > 0) js << ",";
    js << "{\"name\":\"" << JsonEscape(columns[i].name) << "\""
       << ",\"type_name\":\"" << JsonEscape(columns[i].type_name) << "\""
       << ",\"length\":" << columns[i].length
       << ",\"nullable\":" << (columns[i].nullable ? "true" : "false")
       << ",\"is_primary_key\":" << (columns[i].is_primary_key ? "true" : "false") << "}";
  }
  js << "]";
  return js.str();
}

// =============================================================================
// ApiHandler
// =============================================================================

ApiHandler::ApiHandler() = default;
ApiHandler::~ApiHandler() {
  if (client_) {
    client_->Disconnect();
  }
}

ApiHandler::ApiResponse ApiHandler::Connect(const ConnectRequest& req) {
  std::lock_guard<std::mutex> lock(mutex_);

  ApiResponse resp;

  // Disconnect existing connection if any
  if (client_) {
    client_->Disconnect();
    client_.reset();
    connected_ = false;
  }

  client_ = std::make_unique<GoodsDbProtocol>();

  if (!client_->Connect(req.host, req.port)) {
    resp.success = false;
    resp.error = client_->GetLastError();
    if (resp.error.empty()) resp.error = "Connection failed";
    client_.reset();
    return resp;
  }

  if (!client_->Authenticate(req.user, req.password, req.database)) {
    resp.success = false;
    resp.error = client_->GetLastError();
    if (resp.error.empty()) resp.error = "Authentication failed";
    client_->Disconnect();
    client_.reset();
    return resp;
  }

  connected_ = true;
  resp.success = true;
  resp.message = "Connected to " + req.host + ":" + std::to_string(req.port);
  return resp;
}

ApiHandler::ApiResponse ApiHandler::Disconnect() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (client_) {
    client_->Disconnect();
    client_.reset();
  }
  connected_ = false;

  ApiResponse resp;
  resp.success = true;
  resp.message = "Disconnected";
  return resp;
}

bool ApiHandler::IsConnected() {
  std::lock_guard<std::mutex> lock(mutex_);
  return connected_ && client_ && client_->IsConnected();
}

ApiHandler::ExecuteResponse ApiHandler::Execute(const std::string& sql) {
  std::lock_guard<std::mutex> lock(mutex_);

  ExecuteResponse resp;

  if (!connected_ || !client_ || !client_->IsConnected()) {
    resp.success = false;
    resp.error = "Not connected to server";
    return resp;
  }

  ProtoQueryResult result = client_->Execute(sql);

  if (result.is_error) {
    resp.success = false;
    resp.error = result.error_message;
    resp.exec_time_ms = result.exec_time_ms;
    return resp;
  }

  resp.success = true;
  resp.exec_time_ms = result.exec_time_ms;
  resp.affected_rows = result.affected_rows;
  resp.last_insert_id = result.last_insert_id;

  // Convert columns
  for (const auto& col : result.columns) {
    ColumnDef cd;
    cd.name = col.name;
    cd.type_name = col.type_name;
    cd.length = col.length;
    resp.columns.push_back(cd);
  }

  // Convert rows
  for (const auto& row : result.rows) {
    resp.rows.push_back(row);
  }

  return resp;
}

std::vector<std::string> ApiHandler::GetDatabases() {
  auto resp = Execute("SHOW DATABASES");
  if (!resp.success || resp.columns.empty()) return {};

  std::vector<std::string> dbs;
  for (const auto& row : resp.rows) {
    if (!row.empty()) dbs.push_back(row[0]);
  }
  return dbs;
}

std::vector<ApiHandler::TableInfo> ApiHandler::GetTables(const std::string& db) {
  auto resp = Execute("SHOW TABLES FROM `" + db + "`");
  if (!resp.success || resp.columns.empty()) return {};

  std::vector<TableInfo> tables;
  for (const auto& row : resp.rows) {
    if (!row.empty()) tables.push_back({row[0]});
  }
  return tables;
}

std::vector<ApiHandler::ColumnInfo> ApiHandler::GetColumns(
    const std::string& db, const std::string& table) {
  auto resp = Execute("SHOW COLUMNS FROM `" + table + "`");
  if (!resp.success || resp.rows.empty()) return {};

  std::vector<ColumnInfo> columns;
  for (const auto& row : resp.rows) {
    if (row.size() >= 2) {
      ColumnInfo col;
      col.name = row[0];
      col.type_name = row[1];
      col.length = 0;
      // row[2] = Null (YES/NO), row[3] = Key (PRI/UNI/MUL/"")
      if (row.size() >= 3) {
        col.nullable = (row[2] == "YES");
      }
      if (row.size() >= 4) {
        col.is_primary_key = (row[3] == "PRI");
      }
      columns.push_back(col);
    }
  }
  return columns;
}

}  // namespace studio
}  // namespace goods_db
