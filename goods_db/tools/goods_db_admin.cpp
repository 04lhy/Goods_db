/**
 * goods_db_admin — command-line administration tool for goods_db server.
 *
 * Usage:
 *   goods_db_admin [--host HOST] [--port PORT] [--user USER] [--password PWD]
 *                  <command> [args]
 *
 * Commands:
 *   ping              Check server connectivity
 *   status [--ext]    Show server status with optional extended info
 *   version           Show server version
 *   processlist       Show active connections (SHOW PROCESSLIST)
 *   kill <conn_id>    Terminate a connection
 *   create database <name>   Create a new database
 *   drop database <name>     Drop a database
 *   flush [hosts|logs|tables] Flush server state
 *   reload            Reload privileges (FLUSH PRIVILEGES)
 *   shutdown          Gracefully shut down the server
 *   variables         Show server variables
 */

#include <cstring>
#include <iostream>
#include <iomanip>
#include <string>

#include "network_client.h"

using goods_db::tools::NetworkClient;
using goods_db::tools::QueryResult;

static void PrintUsage(const char* prog) {
  std::cout << "goods_db_admin v1.0.0 — Administration tool for goods_db\n\n"
            << "Usage: " << prog
            << " [--host HOST] [--port PORT] [--user USER] [--password PWD]"
            << " <command> [args]\n\n"
            << "Commands:\n"
            << "  ping                      Check server connectivity\n"
            << "  status [--ext]            Show server status\n"
            << "  version                   Show server version\n"
            << "  processlist               Show active connections\n"
            << "  kill <connection_id>      Terminate a connection\n"
            << "  create database <name>    Create a new database\n"
            << "  drop database <name>      Drop a database\n"
            << "  flush hosts|logs|tables   Flush server state\n"
            << "  reload                    Reload privileges\n"
            << "  shutdown                  Gracefully shut down\n"
            << "  variables                 Show server variables\n\n"
            << "Options:\n"
            << "  --host HOST      Server host (default: 127.0.0.1)\n"
            << "  --port PORT      Server port (default: 3307)\n"
            << "  --user USER      Username (default: root)\n"
            << "  --password PWD   Password (default: empty)\n";
}

static void PrintResult(const QueryResult& result) {
  if (result.is_error) {
    std::cerr << "ERROR [" << result.error_code << "]: "
              << result.error_message << std::endl;
    return;
  }

  if (!result.columns.empty()) {
    // Print column headers
    for (size_t i = 0; i < result.columns.size(); i++) {
      if (i > 0) std::cout << "\t";
      std::cout << result.columns[i].name;
    }
    std::cout << std::endl;

    // Print separator
    for (size_t i = 0; i < result.columns.size(); i++) {
      if (i > 0) std::cout << "\t";
      std::cout << std::string(std::min(result.columns[i].name.size(), size_t(20)), '-');
    }
    std::cout << std::endl;

    // Print rows
    for (const auto& row : result.rows) {
      for (size_t i = 0; i < row.size(); i++) {
        if (i > 0) std::cout << "\t";
        std::cout << row[i];
      }
      std::cout << std::endl;
    }
    std::cout << result.rows.size() << " row(s) returned" << std::endl;
  } else {
    std::cout << "OK";
    if (result.affected_rows > 0) {
      std::cout << " (" << result.affected_rows << " row(s) affected)";
    }
    if (!result.info.empty()) {
      std::cout << " " << result.info;
    }
    std::cout << std::endl;
  }
}

int main(int argc, char* argv[]) {
  std::string host = "127.0.0.1";
  uint16_t port = 3307;
  std::string user = "root";
  std::string password;
  std::string execute_sql;

  // Parse options
  int i = 1;
  for (; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--host" && i + 1 < argc) {
      host = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      port = static_cast<uint16_t>(std::stoi(argv[++i]));
    } else if (arg == "--user" && i + 1 < argc) {
      user = argv[++i];
    } else if (arg == "--password" && i + 1 < argc) {
      password = argv[++i];
    } else if ((arg == "-e" || arg == "--execute") && i + 1 < argc) {
      execute_sql = argv[++i];
    } else {
      break;  // Command starts here
    }
  }

  if (i >= argc && execute_sql.empty()) {
    PrintUsage(argv[0]);
    return 1;
  }

  std::string cmd;
  if (!execute_sql.empty()) {
    cmd = "--execute";  // internal marker
  } else {
    cmd = argv[i++];
  }

  // ---- Commands that don't need a connection ----
  if (cmd == "help" || cmd == "--help") {
    PrintUsage(argv[0]);
    return 0;
  }

  // ---- Connect and authenticate ----
  NetworkClient client;
  if (!client.Connect(host, port)) {
    std::cerr << "ERROR: Failed to connect to " << host << ":" << port << std::endl;
    return 1;
  }

  if (!client.Authenticate(user, password)) {
    std::cerr << "ERROR: Authentication failed for user '" << user << "'"
              << std::endl;
    return 1;
  }

  // ---- Execute command ----
  QueryResult result;
  std::string sql;

  if (cmd == "--execute") {
    sql = execute_sql;
  } else if (cmd == "ping") {
    sql = "SELECT 'Server is alive' AS status";
  } else if (cmd == "status") {
    sql = "SHOW STATUS";
  } else if (cmd == "version") {
    sql = "SELECT '" + client.GetServerVersion() + "' AS version";
  } else if (cmd == "processlist") {
    sql = "SHOW PROCESSLIST";
  } else if (cmd == "kill") {
    if (i >= argc) {
      std::cerr << "ERROR: kill requires a connection ID" << std::endl;
      return 1;
    }
    sql = "KILL " + std::string(argv[i++]);
  } else if (cmd == "create") {
    if (i + 1 >= argc) {
      std::cerr << "ERROR: 'create database' requires a name" << std::endl;
      return 1;
    }
    std::string sub = argv[i++];
    if (sub != "database") {
      std::cerr << "ERROR: unknown subcommand 'create " << sub << "'" << std::endl;
      return 1;
    }
    sql = "CREATE DATABASE " + std::string(argv[i++]);
  } else if (cmd == "drop") {
    if (i + 1 >= argc) {
      std::cerr << "ERROR: 'drop database' requires a name" << std::endl;
      return 1;
    }
    std::string sub = argv[i++];
    if (sub != "database") {
      std::cerr << "ERROR: unknown subcommand 'drop " << sub << "'" << std::endl;
      return 1;
    }
    sql = "DROP DATABASE " + std::string(argv[i++]);
  } else if (cmd == "flush") {
    if (i >= argc) {
      std::cerr << "ERROR: flush requires a target (hosts|logs|tables)"
                << std::endl;
      return 1;
    }
    std::string target = argv[i++];
    if (target == "hosts") {
      sql = "FLUSH HOSTS";
    } else if (target == "logs") {
      sql = "FLUSH LOGS";
    } else if (target == "tables") {
      sql = "FLUSH TABLES";
    } else {
      std::cerr << "ERROR: unknown flush target '" << target
                << "' (expected: hosts|logs|tables)" << std::endl;
      return 1;
    }
  } else if (cmd == "reload") {
    sql = "FLUSH PRIVILEGES";
  } else if (cmd == "shutdown") {
    sql = "SHUTDOWN";
  } else if (cmd == "variables") {
    sql = "SHOW STATUS";
  } else {
    std::cerr << "ERROR: unknown command '" << cmd << "'" << std::endl;
    PrintUsage(argv[0]);
    return 1;
  }

  result = client.Execute(sql);
  PrintResult(result);

  if (result.is_error) {
    return 1;
  }

  return 0;
}
