#pragma once

#include <memory>
#include <string>

#include "httplib.h"
#include "api_handler.h"

namespace goods_db {
namespace studio {

// =============================================================================
// WebServer — lightweight HTTP/WebSocket server for goods_db_studio
//
// Wraps cpp-httplib to serve the web frontend and REST API. Runs on the main
// thread; request handling is threaded by httplib's internal pool.
// =============================================================================
class WebServer {
 public:
  WebServer();
  ~WebServer();

  // Start listening on host:port (default 0.0.0.0:9090)
  bool Start(const std::string& host, uint16_t port);

  // Signal shutdown
  void Stop();

  // Set the web root directory for static file serving
  void SetWebRoot(const std::string& path) { web_root_ = path; }

  bool IsRunning() const { return running_; }

 private:
  void SetupRoutes();

  std::unique_ptr<httplib::Server> http_server_;
  std::unique_ptr<ApiHandler> api_handler_;
  std::string web_root_;
  bool running_ = false;
};

}  // namespace studio
}  // namespace goods_db
