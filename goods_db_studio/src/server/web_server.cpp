#include "server/web_server.h"

#include <iostream>

#include "httplib.h"

namespace goods_db {
namespace studio {

WebServer::WebServer()
    : http_server_(std::make_unique<httplib::Server>()),
      api_handler_(std::make_unique<ApiHandler>()) {}

WebServer::~WebServer() { Stop(); }

void WebServer::SetupRoutes() {
  auto& svr = *http_server_;

  // ---- CORS middleware (allow frontend from any origin) ---------------------
  svr.set_pre_routing_handler([](const httplib::Request& req,
                                  httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods",
                    "GET, POST, PUT, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers",
                    "Content-Type, Authorization");
    if (req.method == "OPTIONS") {
      res.status = 204;
      return httplib::Server::HandlerResponse::Handled;
    }
    return httplib::Server::HandlerResponse::Unhandled;
  });

  // ---- API: Connect ---------------------------------------------------------
  svr.Post("/api/connect", [this](const httplib::Request& req,
                                    httplib::Response& res) {
    // Simple manual JSON parsing for connect request
    ApiHandler::ConnectRequest cr;
    auto find_val = [&](const std::string& key) -> std::string {
      auto pos = req.body.find("\"" + key + "\"");
      if (pos == std::string::npos) return "";
      pos = req.body.find('"', pos + key.size() + 3);  // skip "key":
      if (pos == std::string::npos) return "";
      // Find matching closing quote, handling \"
      size_t end = pos + 1;
      while (end < req.body.size()) {
        if (req.body[end] == '\\' && end + 1 < req.body.size()) {
          end += 2;
        } else if (req.body[end] == '"') {
          break;
        } else {
          end++;
        }
      }
      if (end >= req.body.size()) return "";
      // Unescape
      std::string raw = req.body.substr(pos + 1, end - pos - 1);
      std::string out;
      for (size_t i = 0; i < raw.size(); i++) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
          switch (raw[i + 1]) {
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            default:   out += raw[i]; out += raw[i + 1]; break;
          }
          i++;
        } else {
          out += raw[i];
        }
      }
      return out;
    };
    auto find_int = [&](const std::string& key) -> int {
      auto pos = req.body.find("\"" + key + "\"");
      if (pos == std::string::npos) return 0;
      pos = req.body.find(":", pos);
      if (pos == std::string::npos) return 0;
      // skip whitespace
      while (pos + 1 < req.body.size() &&
             (req.body[pos + 1] == ' ' || req.body[pos + 1] == '\t'))
        pos++;
      auto end = pos + 1;
      while (end < req.body.size() && (isdigit(req.body[end]) || req.body[end] == '.'))
        end++;
      return std::stoi(req.body.substr(pos + 1, end - pos - 1));
    };

    cr.host = find_val("host");
    if (cr.host.empty()) cr.host = "localhost";
    cr.user = find_val("user");
    if (cr.user.empty()) cr.user = "root";
    cr.password = find_val("password");
    cr.database = find_val("db");
    int port = find_int("port");
    if (port > 0) cr.port = static_cast<uint16_t>(port);

    auto resp = api_handler_->Connect(cr);
    res.set_content(ApiHandler::ToJson(resp), "application/json");
  });

  // ---- API: Disconnect ------------------------------------------------------
  svr.Post("/api/disconnect", [this](const httplib::Request& /*req*/,
                                       httplib::Response& res) {
    auto resp = api_handler_->Disconnect();
    res.set_content(ApiHandler::ToJson(resp), "application/json");
  });

  // ---- API: Status ----------------------------------------------------------
  svr.Get("/api/status", [this](const httplib::Request& /*req*/,
                                  httplib::Response& res) {
    ApiHandler::ApiResponse resp;
    resp.success = api_handler_->IsConnected();
    resp.message = resp.success ? "Connected" : "Disconnected";
    res.set_content(ApiHandler::ToJson(resp), "application/json");
  });

  // ---- API: Execute SQL -----------------------------------------------------
  svr.Post("/api/execute", [this](const httplib::Request& req,
                                    httplib::Response& res) {
    // Extract SQL from JSON body: {"sql": "..."}
    // Handle escape sequences properly (e.g. \" inside the SQL)
    std::string sql;
    auto pos = req.body.find("\"sql\"");
    if (pos != std::string::npos) {
      pos = req.body.find('"', pos + 5);
      if (pos != std::string::npos) {
        // Find the matching closing quote, skipping \"
        size_t end = pos + 1;
        while (end < req.body.size()) {
          if (req.body[end] == '\\' && end + 1 < req.body.size()) {
            end += 2;  // skip escaped character
          } else if (req.body[end] == '"') {
            break;  // found closing quote
          } else {
            end++;
          }
        }
        if (end < req.body.size()) {
          // Unescape the SQL string (handle \", \\, \n, \r, \t)
          std::string raw = req.body.substr(pos + 1, end - pos - 1);
          for (size_t i = 0; i < raw.size(); i++) {
            if (raw[i] == '\\' && i + 1 < raw.size()) {
              switch (raw[i + 1]) {
                case '"':  sql += '"';  break;
                case '\\': sql += '\\'; break;
                case 'n':  sql += '\n'; break;
                case 'r':  sql += '\r'; break;
                case 't':  sql += '\t'; break;
                default:   sql += raw[i]; sql += raw[i + 1]; break;
              }
              i++;
            } else {
              sql += raw[i];
            }
          }
        }
      }
    }

    if (sql.empty()) {
      ApiHandler::ExecuteResponse err;
      err.success = false;
      err.error = "No SQL provided";
      res.set_content(ApiHandler::ToJson(err), "application/json");
      return;
    }

    auto resp = api_handler_->Execute(sql);
    res.set_content(ApiHandler::ToJson(resp), "application/json");
  });

  // ---- API: Databases -------------------------------------------------------
  svr.Get("/api/databases", [this](const httplib::Request& /*req*/,
                                     httplib::Response& res) {
    auto dbs = api_handler_->GetDatabases();
    res.set_content(ApiHandler::ToJson(dbs), "application/json");
  });

  // ---- API: Tables ----------------------------------------------------------
  svr.Get("/api/tables", [this](const httplib::Request& req,
                                  httplib::Response& res) {
    std::string db;
    if (req.has_param("db")) {
      db = req.get_param_value("db");
    }
    if (db.empty()) {
      res.set_content("[]", "application/json");
      return;
    }
    auto tables = api_handler_->GetTables(db);
    res.set_content(ApiHandler::ToJson(tables), "application/json");
  });

  // ---- API: Columns ---------------------------------------------------------
  svr.Get("/api/columns", [this](const httplib::Request& req,
                                   httplib::Response& res) {
    std::string db, table;
    if (req.has_param("db")) db = req.get_param_value("db");
    if (req.has_param("table")) table = req.get_param_value("table");
    if (db.empty() || table.empty()) {
      res.set_content("[]", "application/json");
      return;
    }
    auto columns = api_handler_->GetColumns(db, table);
    res.set_content(ApiHandler::ToJson(columns), "application/json");
  });

  // ---- Static file serving --------------------------------------------------
  if (!web_root_.empty()) {
    svr.set_mount_point("/", web_root_);
  }
}

bool WebServer::Start(const std::string& host, uint16_t port) {
  SetupRoutes();
  running_ = true;

  std::cout << "goods_db_studio web server starting on http://"
            << host << ":" << port << std::endl;
  std::cout << "Open your browser and navigate to the address above."
            << std::endl;

  // httplib::Server::listen() blocks until the server stops.
  // We wrap it so Start() returns false on failure.
  return http_server_->listen(host, port);
}

void WebServer::Stop() {
  if (running_) {
    http_server_->stop();
    running_ = false;
  }
}

}  // namespace studio
}  // namespace goods_db
