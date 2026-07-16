#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "server/web_server.h"

int main(int argc, char* argv[]) {
  std::string host = "0.0.0.0";
  uint16_t port = 8080;

  // Parse command line
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--host" && i + 1 < argc) {
      host = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      port = static_cast<uint16_t>(std::stoi(argv[++i]));
    } else if (arg == "--help") {
      std::cout << "goods_db_studio web server\n"
                << "Usage: " << argv[0] << " [options]\n"
                << "  --host HOST   Bind address (default: 0.0.0.0)\n"
                << "  --port PORT   Listen port (default: 8080)\n"
                << "  --help        Show this help\n";
      return 0;
    }
  }

  // Determine web root relative to executable
  // Try common locations: ../web, ../../web, ./web, absolute path
  std::string web_root;
  auto try_web_root = [&](const std::string& path) -> bool {
    std::ifstream test(path + "/index.html");
    if (test.good()) { web_root = path; return true; }
    return false;
  };

  if (!try_web_root("../web") &&
      !try_web_root("../../web") &&
      !try_web_root("./web") &&
      !try_web_root("../goods_db_studio/web") &&
      !try_web_root("goods_db_studio/web")) {
    // Try to locate via executable path
    std::string exe_path = argv[0];
    auto slash = exe_path.rfind('/');
    if (slash != std::string::npos) {
      std::string exe_dir = exe_path.substr(0, slash);
      if (try_web_root(exe_dir + "/../web") ||
          try_web_root(exe_dir + "/../../web")) {
        // found
      }
    }
  }

  if (web_root.empty()) {
    std::cerr << "ERROR: Cannot find web root directory (index.html not found)\n";
    return 1;
  }
  std::cout << "Web root: " << web_root << std::endl;

  goods_db::studio::WebServer server;
  server.SetWebRoot(web_root);

  if (!server.Start(host, port)) {
    std::cerr << "Failed to start web server on " << host << ":" << port << "\n";
    return 1;
  }

  return 0;
}
