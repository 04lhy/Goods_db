/**
 * goods_db_dump — backup and restore tool for goods_db server.
 *
 * Usage:
 *   goods_db_dump [options] [database]
 *
 * Options:
 *   --host HOST          Server host (default: 127.0.0.1)
 *   --port PORT          Server port (default: 3307)
 *   --user USER          Username (default: root)
 *   --password PWD       Password (default: empty)
 *   --result-file PATH   Output file (default: stdout)
 *   --full               Full backup (all tables → CREATE + INSERT)
 *   --no-create-info     Skip CREATE TABLE statements
 *   --no-data            Skip INSERT statements
 *   --complete-insert    Use complete INSERT with column names
 *   --single-transaction Dump in a single transaction
 */

#include <ctime>
#include <fstream>
#include <iostream>
#include <string>

#include "network_client.h"

using goods_db::tools::NetworkClient;
using goods_db::tools::QueryResult;

static void PrintUsage(const char* prog) {
  std::cout << "goods_db_dump v1.0.0 — Backup tool for goods_db\n\n"
            << "Usage: " << prog
            << " [options] [database]\n\n"
            << "Options:\n"
            << "  --host HOST            Server host (default: 127.0.0.1)\n"
            << "  --port PORT            Server port (default: 3307)\n"
            << "  --user USER            Username (default: root)\n"
            << "  --password PWD         Password (default: empty)\n"
            << "  --result-file PATH     Output file (default: stdout)\n"
            << "  --full                 Full backup (DDL + data)\n"
            << "  --no-create-info       Skip CREATE TABLE statements\n"
            << "  --no-data              Skip INSERT statements\n"
            << "  --complete-insert      Use INSERT INTO t(cols) VALUES\n"
            << "  --single-transaction   Dump in a single transaction\n\n"
            << "Examples:\n"
            << "  goods_db_dump --result-file=backup.sql --full\n"
            << "  goods_db_dump --no-data > schema.sql\n";
}

static std::string EscapeString(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '\'') out += "\\'";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\t') out += "\\t";
    else out += c;
  }
  return out;
}

static std::string QuoteValue(const std::string& val, const std::string& type) {
  if (val == "\\N") return "NULL";
  if (type == "INTEGER" || type == "BIGINT" || type == "SMALLINT" ||
      type == "TINYINT" || type == "DECIMAL" || type == "BOOLEAN") {
    return val;
  }
  return "'" + EscapeString(val) + "'";
}

int main(int argc, char* argv[]) {
  std::string host = "127.0.0.1";
  uint16_t port = 3307;
  std::string user = "root";
  std::string password;
  std::string result_file;
  bool full = false;
  bool no_create_info = false;
  bool no_data = false;
  bool complete_insert = false;
  bool single_transaction = false;
  std::string database;

  // Parse options
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--host" && i + 1 < argc) {
      host = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      port = static_cast<uint16_t>(std::stoi(argv[++i]));
    } else if (arg == "--user" && i + 1 < argc) {
      user = argv[++i];
    } else if (arg == "--password" && i + 1 < argc) {
      password = argv[++i];
    } else if (arg.rfind("--result-file=", 0) == 0) {
      result_file = arg.substr(14);
    } else if (arg == "--full") {
      full = true;
    } else if (arg == "--no-create-info") {
      no_create_info = true;
    } else if (arg == "--no-data") {
      no_data = true;
    } else if (arg == "--complete-insert") {
      complete_insert = true;
    } else if (arg == "--single-transaction") {
      single_transaction = true;
    } else if (arg == "--help" || arg == "help") {
      PrintUsage(argv[0]);
      return 0;
    } else if (arg[0] != '-') {
      database = arg;
    }
  }

  (void)full;  // full mode is the default behavior

  // Open output
  std::ofstream fout;
  std::ostream* out = &std::cout;
  if (!result_file.empty()) {
    fout.open(result_file);
    if (!fout.is_open()) {
      std::cerr << "ERROR: Cannot open output file: " << result_file << std::endl;
      return 1;
    }
    out = &fout;
  }

  // Connect
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

  // Header
  *out << "-- goods_db database dump\n"
       << "-- Server version: " << client.GetServerVersion() << "\n"
       << "-- Date: " << std::time(nullptr) << "\n\n";

  if (single_transaction) {
    *out << "START TRANSACTION;\n\n";
  }

  // Get table list
  QueryResult tables_result = client.Execute("SHOW TABLES");
  if (tables_result.is_error) {
    std::cerr << "ERROR: Failed to list tables: "
              << tables_result.error_message << std::endl;
    return 1;
  }

  // If no columns or it's an OK response (no result set), try alternate approach
  std::vector<std::string> table_names;
  if (tables_result.columns.empty()) {
    // No result set - the SHOW TABLES command might not be supported
    std::cerr << "WARNING: SHOW TABLES returned no result set" << std::endl;
  } else {
    for (const auto& row : tables_result.rows) {
      if (!row.empty()) {
        table_names.push_back(row[0]);
      }
    }
  }

  if (table_names.empty()) {
    std::cerr << "No tables found to dump." << std::endl;
    if (single_transaction) *out << "COMMIT;\n";
    return 0;
  }

  *out << "USE goods_db;\n\n";

  // Dump each table
  for (const auto& table : table_names) {
    if (!no_create_info) {
      // Generate CREATE TABLE from column info
      QueryResult cols_result = client.Execute("SHOW COLUMNS FROM " + table);
      if (cols_result.is_error) {
        std::cerr << "WARNING: Cannot get columns for table " << table
                  << ": " << cols_result.error_message << std::endl;
        continue;
      }

      *out << "-- Table: " << table << "\n";
      *out << "DROP TABLE IF EXISTS " << table << ";\n";
      *out << "CREATE TABLE " << table << " (\n";

      for (size_t i = 0; i < cols_result.rows.size(); i++) {
        const auto& row = cols_result.rows[i];
        // SHOW COLUMNS returns: Field, Type, Null, Key, Default, Extra
        std::string field = row.size() > 0 ? row[0] : "";
        std::string type = row.size() > 1 ? row[1] : "";
        std::string nullable = row.size() > 2 ? row[2] : "YES";

        *out << "  " << field << " " << type;
        if (nullable == "NO") *out << " NOT NULL";
        if (i + 1 < cols_result.rows.size()) *out << ",";
        *out << "\n";
      }
      *out << ");\n\n";
    }

    if (!no_data) {
      // Dump data
      QueryResult data_result = client.Execute("SELECT * FROM " + table);
      if (data_result.is_error) {
        std::cerr << "WARNING: Cannot read data from table " << table
                  << ": " << data_result.error_message << std::endl;
        continue;
      }

      if (!data_result.rows.empty()) {
        // Batch INSERT (up to 100 rows per statement)
        size_t batch_size = 100;
        for (size_t start = 0; start < data_result.rows.size(); start += batch_size) {
          size_t end = std::min(start + batch_size, data_result.rows.size());

          if (complete_insert && !data_result.columns.empty()) {
            *out << "INSERT INTO " << table << " (";
            for (size_t c = 0; c < data_result.columns.size(); c++) {
              if (c > 0) *out << ", ";
              *out << data_result.columns[c].name;
            }
            *out << ") VALUES\n";
          } else {
            *out << "INSERT INTO " << table << " VALUES\n";
          }

          for (size_t r = start; r < end; r++) {
            *out << "  (";
            const auto& row = data_result.rows[r];
            for (size_t c = 0; c < row.size(); c++) {
              if (c > 0) *out << ", ";
              std::string type = (c < data_result.columns.size())
                                    ? data_result.columns[c].type
                                    : "";
              *out << QuoteValue(row[c], type);
            }
            *out << ")";
            if (r + 1 < end) *out << ",";
            *out << "\n";
          }
          *out << ";\n\n";
        }
      }
    }
  }

  if (single_transaction) {
    *out << "COMMIT;\n";
  }

  *out << "-- Dump completed: " << table_names.size() << " table(s)\n";

  if (fout.is_open()) {
    fout.close();
    std::cout << "Backup written to: " << result_file << std::endl;
  }

  return 0;
}
