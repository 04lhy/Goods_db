#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "server/web_server.h"

int main(int argc, char* argv[]) {
  std::string host = "0.0.0.0";
  uint16_t port = 9090;

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
                << "  --port PORT   Listen port (default: 9090)\n"
                << "  --help        Show this help\n";
      return 0;
    }
  }

  // Determine web root relative to executable
  // Try common locations: ../web, ../../web, ./web
  std::string web_root = "../web";
  {
    std::ifstream test(web_root + "/index.html");
    if (!test.good()) {
      web_root = "../../web";
      test.open(web_root + "/index.html");
      if (!test.good()) {
        web_root = "./web";
        test.open(web_root + "/index.html");
        if (!test.good()) {
          web_root = "../goods_db_studio/web";
        }
      }
    }
  }

  goods_db::studio::WebServer server;
  server.SetWebRoot(web_root);

  if (!server.Start(host, port)) {
    std::cerr << "Failed to start web server on " << host << ":" << port << "\n";
    return 1;
  }

  return 0;
}
