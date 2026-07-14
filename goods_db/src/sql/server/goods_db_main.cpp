#include <arpa/inet.h>
#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "buffer/clock_replacer.h"
#include "common/lock_manager.h"
#include "common/logger.h"
#include "common/transaction_manager.h"
#include "sql/executor/execution_engine.h"
#include "sql/goods_handler.h"
#include "sql/handlerton.h"
#include "sql/optimizer/optimizer.h"
#include "sql/log/log_manager.h"
#include "sql/network/net_serv.h"
#include "sql/security/auth_manager.h"
#include "sql/server/connection_handler.h"
#include "sql/server/thread_pool.h"

namespace {

std::atomic<bool> server_running{true};

void SignalHandler(int sig) {
  if (sig == SIGTERM || sig == SIGINT) {
    server_running = false;
  }
}

void PrintUsage(const char* prog) {
  std::cout << "goods_db server v0.1.0\n"
            << "Usage: " << prog << " [options]\n"
            << "Options:\n"
            << "  --host HOST       Bind address (default: 0.0.0.0)\n"
            << "  --port PORT       Listen port (default: 3307)\n"
            << "  --threads N       Thread pool size (default: auto)\n"
            << "  --datadir DIR     Data directory (default: ./data)\n"
            << "  --help            Show this help\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  std::string host = "0.0.0.0";
  uint16_t port = 3307;
  size_t thread_pool_size = 0;
  std::string datadir = "./data";

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--host" && i + 1 < argc) {
      host = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      port = static_cast<uint16_t>(std::stoi(argv[++i]));
    } else if (arg == "--threads" && i + 1 < argc) {
      thread_pool_size = std::stoul(argv[++i]);
    } else if (arg == "--datadir" && i + 1 < argc) {
      datadir = argv[++i];
    } else if (arg == "--help") {
      PrintUsage(argv[0]);
      return 0;
    }
  }

  // Create data directory if it doesn't exist
  mkdir(datadir.c_str(), 0755);

  std::signal(SIGTERM, SignalHandler);
  std::signal(SIGINT, SignalHandler);

  goods_db::SetLogLevel(goods_db::LogLevel::INFO);

  std::cout << "goods_db v0.1.0 starting..." << std::endl;
  LOG_INFO("Server starting on {}:{}", host, port);

  // ---- Storage layer -------------------------------------------------------
  std::cout << "[1/6] Initializing storage layer..." << std::endl;
  auto disk_mgr = std::make_unique<goods_db::DiskManager>();
  auto replacer = std::make_unique<goods_db::ClockReplacer>(100);
  auto bpm = std::make_unique<goods_db::BufferPoolManager>(
      100, disk_mgr.get(), std::move(replacer));
  auto catalog = std::make_unique<goods_db::Catalog>(bpm.get());
  std::cout << "[1/6] Storage layer OK" << std::endl;

  // ---- Storage engine ------------------------------------------------------
  std::cout << "[2/6] Registering storage engine..." << std::endl;
  goods_db::handlerton* hton = goods_db::get_engine("goods_engine");
  if (!hton) {
    auto* global_hton = new goods_db::handlerton();
    global_hton->name = "goods_engine";
    global_hton->init = goods_db::goods_handler::engine_init;
    global_hton->deinit = goods_db::goods_handler::engine_deinit;
    global_hton->create_handler =
        goods_db::goods_handler::engine_create_handler;
    global_hton->flags = goods_db::HTON_FLAG_SUPPORTS_INDEXES;
    goods_db::register_engine(global_hton);
  }
  std::cout << "[2/6] Storage engine OK" << std::endl;

  // ---- Auth + Log + Lock + Txn --------------------------------------------
  std::cout << "[3/6] Initializing AuthManager..." << std::endl;
  goods_db::AuthManager auth_mgr;
  auth_mgr.Initialize(catalog.get(), bpm.get());
  std::cout << "[3/6] AuthManager OK (root@localhost, no password)" << std::endl;

  std::cout << "[4/6] Initializing LogManager + LockManager + TransactionManager..."
            << std::endl;
  goods_db::LogManager log_mgr;
  log_mgr.Initialize(datadir);

  goods_db::LockManager lock_mgr;
  goods_db::TransactionManager txn_mgr;
  txn_mgr.Initialize(datadir + "/goods_db_xid.dat");
  std::cout << "[4/6] LogManager + LockManager + TransactionManager OK" << std::endl;

  // ---- Engine instance -----------------------------------------------------
  std::cout << "[5/6] Creating engine + execution engine..." << std::endl;
  goods_db::goods_handler engine(bpm.get(), disk_mgr.get(), catalog.get());
  goods_db::ExecutionEngine exec_engine(bpm.get(), disk_mgr.get(), catalog.get());
  // Wire managers into the execution engine for administrative commands
  exec_engine.SetAuthManager(&auth_mgr);
  exec_engine.SetLogManager(&log_mgr);
  exec_engine.SetLockManager(&lock_mgr);
  exec_engine.SetTransactionManager(&txn_mgr);
  std::cout << "[5/6] Engine OK" << std::endl;

  // ---- Thread pool + Listen ------------------------------------------------
  std::cout << "[6/6] Starting thread pool and listening..." << std::endl;
  goods_db::ThreadPool pool(thread_pool_size);
  LOG_INFO("ThreadPool started with {} workers", pool.GetWorkerCount());

  goods_db::SocketServer server;
  if (!server.Listen(host, port)) {
    std::cerr << "FATAL: Failed to listen on " << host << ":" << port << "\n";
    LOG_ERROR("Failed to listen on {}:{}", host, port);
    return 1;
  }

  std::cout << "==============================================" << std::endl;
  std::cout << " Server listening on " << host << ":" << port << std::endl;
  std::cout << " Press Ctrl+C to stop" << std::endl;
  std::cout << "==============================================" << std::endl;
  LOG_INFO("Listening on {}:{}", host, port);

  // ---- Main accept loop ----------------------------------------------------
  while (server_running) {
    int client_fd = server.Accept();
    if (client_fd < 0) {
      if (errno == EINTR) continue;
      LOG_ERROR("Accept failed: {}", std::strerror(errno));
      continue;
    }

    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    char ip_str[INET_ADDRSTRLEN] = "unknown";
    uint16_t client_port = 0;
    if (getpeername(client_fd, reinterpret_cast<struct sockaddr*>(&addr),
                    &addr_len) == 0) {
      inet_ntop(AF_INET, &addr.sin_addr, ip_str, sizeof(ip_str));
      client_port = ntohs(addr.sin_port);
    }

    LOG_INFO("New connection from {}:{}", ip_str, client_port);

    try {
      pool.Submit([client_fd, ip = std::string(ip_str), client_port, &engine,
                   &auth_mgr, &log_mgr, &lock_mgr, &exec_engine]() {
        goods_db::ConnectionHandler handler(client_fd, ip, client_port,
                                            &engine, &auth_mgr, &log_mgr,
                                            &lock_mgr, &exec_engine);
        handler.Run();
      });
    } catch (const std::exception& e) {
      LOG_ERROR("Failed to submit connection: {}", e.what());
      close(client_fd);
    }
  }

  // ---- Graceful shutdown ---------------------------------------------------
  std::cout << "\nShutting down..." << std::endl;
  LOG_INFO("Server shutting down...");

  server.Close();
  pool.Shutdown();
  log_mgr.Shutdown();

  std::cout << "Server stopped." << std::endl;
  return 0;
}
