#pragma once

/**
 * Minimal TCP client for goods_db server communication.
 * Uses POSIX sockets with zero external dependencies.
 *
 * Wire protocol (packet-framed):
 *   Each packet: [4-byte header: len(3B) + seq_id(1B)][payload]
 *   Length is little-endian 3-byte integer.
 *
 * Auth flow:
 *   1. Server sends packet: "goods_db X.Y.Z\n"
 *   2. Client sends auth packet: "user\0password\0"
 *   3. Server responds: "OK ...\n" or "ERR ...\n"
 *   4. Client sends SQL query packet (raw text)
 *   5. Server responds with result set or OK/ERR
 */

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace goods_db {
namespace tools {

struct QueryResult {
  struct Column {
    std::string name;
    std::string type;
    uint32_t length = 0;
  };
  std::vector<Column> columns;
  std::vector<std::vector<std::string>> rows;
  uint64_t affected_rows = 0;
  uint64_t last_insert_id = 0;
  std::string info;
  bool is_error = false;
  uint16_t error_code = 0;
  std::string error_message;
};

class NetworkClient {
 public:
  NetworkClient() = default;
  ~NetworkClient() { Disconnect(); }

  bool Connect(const std::string& host, uint16_t port) {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) return false;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
      struct hostent* he = gethostbyname(host.c_str());
      if (!he) { close(fd_); fd_ = -1; return false; }
      memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    if (connect(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
      close(fd_); fd_ = -1; return false;
    }

    // Read server greeting (packet-framed)
    std::string greeting = ReadPacket();
    if (greeting.empty()) { Disconnect(); return false; }
    // Strip trailing newline
    if (!greeting.empty() && greeting.back() == '\n')
      greeting.pop_back();
    server_version_ = greeting;
    return true;
  }

  bool Authenticate(const std::string& user, const std::string& password) {
    std::string auth_pkt = user + '\0' + password + '\0';
    if (!WritePacket(auth_pkt)) return false;

    std::string response = ReadPacket();
    if (response.empty()) return false;

    // Strip trailing newline
    if (!response.empty() && response.back() == '\n')
      response.pop_back();

    if (response.rfind("OK", 0) == 0) {
      return true;
    }
    if (response.rfind("ERR", 0) == 0) {
      last_error_msg_ = response;
      return false;
    }
    return false;
  }

  QueryResult Execute(const std::string& sql) {
    QueryResult result;
    if (fd_ < 0) {
      result.is_error = true;
      result.error_message = "Not connected";
      return result;
    }

    if (!WritePacket(sql)) {
      result.is_error = true;
      result.error_message = "Write failed";
      return result;
    }

    return ReadResult();
  }

  void Disconnect() {
    if (fd_ >= 0) { close(fd_); fd_ = -1; }
  }

  bool IsConnected() const { return fd_ >= 0; }
  const std::string& GetServerVersion() const { return server_version_; }

 private:
  int fd_ = -1;
  std::string server_version_;
  std::string last_error_msg_;

  // ---- Packet-framed I/O ------------------------------------------------------

  static uint32_t ReadPacketLength(const uint8_t* header) {
    return header[0] | (static_cast<uint32_t>(header[1]) << 8) |
           (static_cast<uint32_t>(header[2]) << 16);
  }

  static void WritePacketLength(uint8_t* header, uint32_t len) {
    header[0] = static_cast<uint8_t>(len & 0xFF);
    header[1] = static_cast<uint8_t>((len >> 8) & 0xFF);
    header[2] = static_cast<uint8_t>((len >> 16) & 0xFF);
    header[3] = 0;
  }

  std::string ReadPacket() {
    // Read 4-byte header
    uint8_t header[4];
    if (!ReadAll(reinterpret_cast<char*>(header), 4)) return "";

    uint32_t payload_len = ReadPacketLength(header);
    if (payload_len == 0 || payload_len > 64 * 1024 * 1024) return "";

    // Read payload
    std::string payload(payload_len, '\0');
    if (!ReadAll(&payload[0], payload_len)) return "";

    return payload;
  }

  bool WritePacket(const std::string& data) {
    uint8_t header[4];
    WritePacketLength(header, static_cast<uint32_t>(data.size()));
    if (!WriteAll(reinterpret_cast<const char*>(header), 4)) return false;
    if (!WriteAll(data.data(), data.size())) return false;
    return true;
  }

  bool ReadAll(char* buf, size_t len) {
    if (fd_ < 0) return false;
    size_t total = 0;
    while (total < len) {
      ssize_t n = recv(fd_, buf + total, len - total, 0);
      if (n <= 0) return false;
      total += static_cast<size_t>(n);
    }
    return true;
  }

  bool WriteAll(const char* data, size_t len) {
    if (fd_ < 0) return false;
    size_t total = 0;
    while (total < len) {
      ssize_t n = send(fd_, data + total, len - total, 0);
      if (n <= 0) return false;
      total += static_cast<size_t>(n);
    }
    return true;
  }

  // ---- Result parsing ---------------------------------------------------------

  QueryResult ReadResult() {
    QueryResult result;

    // Result may span multiple packets
    std::string combined;
    while (true) {
      std::string pkt = ReadPacket();
      if (pkt.empty()) break;
      combined += pkt;
      // Check if this was the last packet (ends with OK, ERR, EOF)
      if (pkt.rfind("OK", 0) == 0 || pkt.rfind("ERR", 0) == 0) break;
      if (pkt.find("EOF\n") != std::string::npos) break;
      if (pkt.find("\nOK ") != std::string::npos) break;
    }

    if (combined.empty()) {
      result.is_error = true;
      result.error_message = "Empty response";
      return result;
    }

    // Split into lines for parsing
    std::istringstream stream(combined);
    std::string line;
    std::getline(stream, line);

    if (line.rfind("ERR", 0) == 0) {
      result.is_error = true;
      // Parse: "ERR code state message"
      std::istringstream iss(line);
      std::string err_tag;
      iss >> err_tag >> result.error_code;
      std::string state;
      iss >> state;
      std::getline(iss, result.error_message);
      if (!result.error_message.empty() && result.error_message[0] == ' ')
        result.error_message = result.error_message.substr(1);
      return result;
    }

    if (line.rfind("OK", 0) == 0) {
      ParseOk(line, &result);
      return result;
    }

    if (line.rfind("Columns:", 0) == 0) {
      // Parse column count
      uint32_t num_columns = std::stoul(line.substr(9));

      // Read column definitions
      for (uint32_t i = 0; i < num_columns; i++) {
        std::getline(stream, line);
        if (line.rfind("ColDef: ", 0) != 0) continue;
        std::string coldef = line.substr(8);
        size_t sp1 = coldef.find(' ');
        size_t sp2 = coldef.find(' ', sp1 + 1);
        if (sp1 != std::string::npos && sp2 != std::string::npos) {
          QueryResult::Column col;
          col.name = coldef.substr(0, sp1);
          col.type = coldef.substr(sp1 + 1, sp2 - sp1 - 1);
          col.length = std::stoul(coldef.substr(sp2 + 1));
          result.columns.push_back(col);
        }
      }

      // Read "#ROWS" marker
      std::getline(stream, line);

      // Read row data
      while (std::getline(stream, line)) {
        if (line == "EOF") break;
        if (line.rfind("OK ", 0) == 0) {
          ParseOk(line, &result);
          break;
        }
        if (line.rfind("ERR", 0) == 0) {
          result.is_error = true;
          result.error_message = line;
          break;
        }
        if (line.rfind("Row: ", 0) == 0) {
          std::string data = line.substr(5);
          std::vector<std::string> row;
          size_t pos = 0;
          while (pos < data.size()) {
            size_t tab = data.find('\t', pos);
            if (tab == std::string::npos) {
              row.push_back(data.substr(pos));
              break;
            }
            row.push_back(data.substr(pos, tab - pos));
            pos = tab + 1;
          }
          result.rows.push_back(row);
        }
      }
    }

    return result;
  }

  void ParseOk(const std::string& line, QueryResult* result) {
    std::istringstream iss(line);
    std::string ok_tag;
    iss >> ok_tag >> result->affected_rows >> result->last_insert_id;
    std::getline(iss, result->info);
    if (!result->info.empty() && result->info[0] == ' ')
      result->info = result->info.substr(1);
  }
};

}  // namespace tools
}  // namespace goods_db
