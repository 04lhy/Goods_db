#include "network/goods_db_protocol.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>

namespace goods_db {
namespace studio {

// =============================================================================
// Packet helpers
// =============================================================================

static std::string BuildPacket(const std::string& data) {
  uint32_t len = static_cast<uint32_t>(data.size());
  std::string pkt;
  pkt.reserve(4 + data.size());
  pkt.push_back(static_cast<char>(len & 0xFF));
  pkt.push_back(static_cast<char>((len >> 8) & 0xFF));
  pkt.push_back(static_cast<char>((len >> 16) & 0xFF));
  pkt.push_back(static_cast<char>(0));  // sequence_id = 0
  pkt += data;
  return pkt;
}

// =============================================================================
// GoodsDbProtocol
// =============================================================================

GoodsDbProtocol::GoodsDbProtocol() = default;

GoodsDbProtocol::~GoodsDbProtocol() {
  Disconnect();
}

// ---- Low-level I/O ----------------------------------------------------------

bool GoodsDbProtocol::WaitReadable(int timeout_ms) {
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(sock_fd_, &fds);

  struct timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;

  int rc = select(sock_fd_ + 1, &fds, nullptr, nullptr, &tv);
  return rc > 0;
}

bool GoodsDbProtocol::ReadExact(char* buf, size_t count, int timeout_ms) {
  size_t got = 0;
  while (got < count) {
    if (!WaitReadable(timeout_ms)) return false;
    ssize_t n = read(sock_fd_, buf + got, count - got);
    if (n <= 0) {
      if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
      return false;
    }
    got += static_cast<size_t>(n);
  }
  return true;
}

bool GoodsDbProtocol::WriteAll(const char* data, size_t len) {
  size_t written = 0;
  while (written < len) {
    ssize_t n = write(sock_fd_, data + written, len - written);
    if (n <= 0) {
      if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
      return false;
    }
    written += static_cast<size_t>(n);
  }
  return true;
}

// ---- Packet I/O -------------------------------------------------------------

bool GoodsDbProtocol::WritePacket(const std::string& data) {
  std::string pkt = BuildPacket(data);
  return WriteAll(pkt.data(), pkt.size());
}

bool GoodsDbProtocol::ReadPacket(std::string& payload, int timeout_ms) {
  // Read 4-byte header
  char hdr[4];
  if (!ReadExact(hdr, 4, timeout_ms)) return false;

  uint32_t plen = static_cast<uint8_t>(hdr[0]) |
                  (static_cast<uint32_t>(static_cast<uint8_t>(hdr[1])) << 8) |
                  (static_cast<uint32_t>(static_cast<uint8_t>(hdr[2])) << 16);

  if (plen > 16 * 1024 * 1024) return false;  // 16 MB limit
  if (plen == 0) {
    payload.clear();
    return true;
  }

  payload.resize(plen);
  return ReadExact(&payload[0], plen, timeout_ms);
}

// ---- Connection -------------------------------------------------------------

bool GoodsDbProtocol::Connect(const std::string& host, uint16_t port,
                               int timeout_ms) {
  sock_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_fd_ < 0) {
    SetError("Failed to create socket");
    return false;
  }

  // Set non-blocking for connect with timeout
  int flags = fcntl(sock_fd_, F_GETFL, 0);
  fcntl(sock_fd_, F_SETFL, flags | O_NONBLOCK);

  struct sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  if (host == "localhost" || host == "127.0.0.1") {
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  } else if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    SetError("Invalid host address");
    close(sock_fd_);
    sock_fd_ = -1;
    return false;
  }

  int rc = connect(sock_fd_, reinterpret_cast<struct sockaddr*>(&addr),
                    sizeof(addr));
  if (rc < 0 && errno != EINPROGRESS) {
    SetError(std::string("Connect failed: ") + strerror(errno));
    close(sock_fd_);
    sock_fd_ = -1;
    return false;
  }

  if (rc < 0) {
    // Wait for connection to complete
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sock_fd_, &wfds);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    rc = select(sock_fd_ + 1, nullptr, &wfds, nullptr, &tv);
    if (rc <= 0) {
      SetError("Connection timeout");
      close(sock_fd_);
      sock_fd_ = -1;
      return false;
    }

    // Check if connection succeeded
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(sock_fd_, SOL_SOCKET, SO_ERROR, &err, &len);
    if (err != 0) {
      SetError(std::string("Connect failed: ") + strerror(err));
      close(sock_fd_);
      sock_fd_ = -1;
      return false;
    }
  }

  // Set back to blocking
  fcntl(sock_fd_, F_SETFL, flags);

  return true;
}

void GoodsDbProtocol::Disconnect() {
  if (sock_fd_ >= 0) {
    close(sock_fd_);
    sock_fd_ = -1;
  }
}

bool GoodsDbProtocol::IsConnected() const {
  return sock_fd_ >= 0;
}

// ---- Authentication ---------------------------------------------------------

bool GoodsDbProtocol::Authenticate(const std::string& user,
                                    const std::string& password,
                                    const std::string& db) {
  if (sock_fd_ < 0) {
    SetError("Not connected");
    return false;
  }

  // Step 1: Read server greeting
  std::string greeting;
  if (!ReadPacket(greeting, 5000)) {
    SetError("No server greeting");
    return false;
  }

  // Step 2: Send auth packet: "AUTH\0user\0password\0db\0"
  std::string auth = "AUTH";
  auth.push_back('\0');
  auth += user;
  auth.push_back('\0');
  auth += password;
  auth.push_back('\0');
  auth += db;
  auth.push_back('\0');

  if (!WritePacket(auth)) {
    SetError("Failed to send auth");
    return false;
  }

  // Step 3: Read auth response
  std::string resp;
  if (!ReadPacket(resp, 5000)) {
    SetError("No auth response");
    return false;
  }

  if (resp.rfind("OK ", 0) == 0) return true;
  if (resp.rfind("ERR ", 0) == 0) {
    SetError(resp.substr(4));
    return false;
  }

  SetError("Unexpected auth response: " + resp);
  return false;
}

// ---- Commands ---------------------------------------------------------------

bool GoodsDbProtocol::Ping() {
  if (sock_fd_ < 0) return false;

  if (!WritePacket("PING")) return false;

  std::string resp;
  if (!ReadPacket(resp, 3000)) return false;

  return resp.rfind("OK", 0) == 0;
}

ProtoQueryResult GoodsDbProtocol::Execute(const std::string& sql) {
  ProtoQueryResult result;
  auto start = std::chrono::steady_clock::now();

  if (sock_fd_ < 0) {
    result.is_error = true;
    result.error_message = "Not connected";
    return result;
  }

  // Build query packet: "QUERY\0<sql>"
  std::string query = "QUERY";
  query.push_back('\0');
  query += sql;

  if (!WritePacket(query)) {
    result.is_error = true;
    result.error_message = "Failed to send query";
    return result;
  }

  // Read response
  std::string response;
  if (!ReadPacket(response, 30000)) {
    result.is_error = true;
    result.error_message = "No response from server";
    return result;
  }

  result = ParseResponse(response);

  auto end = std::chrono::steady_clock::now();
  result.exec_time_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

  return result;
}

// ---- Response parsing -------------------------------------------------------

ProtoQueryResult GoodsDbProtocol::ParseResponse(const std::string& data) {
  if (data.rfind("OK ", 0) == 0 || data.rfind("ERR ", 0) == 0) {
    return ParseOkError(data);
  }
  return ParseResultSet(data);
}

ProtoQueryResult GoodsDbProtocol::ParseResultSet(const std::string& data) {
  ProtoQueryResult result;
  std::string text = data;
  size_t pos = 0;

  while (pos < text.size()) {
    // Find end of line
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) break;
    std::string line = text.substr(pos, end - pos);
    pos = end + 1;

    if (line.empty()) continue;

    if (line.rfind("Columns: ", 0) == 0) {
      continue;
    }
    if (line.rfind("ColDef: ", 0) == 0) {
      // Format: "ColDef: name type [length]"
      std::string rest = line.substr(8);
      size_t s1 = rest.find(' ');
      if (s1 != std::string::npos) {
        ProtoColumnInfo col;
        col.name = rest.substr(0, s1);
        size_t s2 = rest.find(' ', s1 + 1);
        if (s2 != std::string::npos) {
          col.type_name = rest.substr(s1 + 1, s2 - s1 - 1);
          col.length = std::stoi(rest.substr(s2 + 1));
        } else {
          col.type_name = rest.substr(s1 + 1);
        }
        result.columns.push_back(col);
      }
      continue;
    }
    if (line == "#ROWS" || line == "EOF") {
      continue;
    }
    if (line.rfind("Row: ", 0) == 0) {
      std::string row_text = line.substr(5);
      std::vector<std::string> row;
      size_t tab_pos = 0;
      while (tab_pos < row_text.size()) {
        size_t next_tab = row_text.find('\t', tab_pos);
        std::string val = row_text.substr(tab_pos,
            next_tab == std::string::npos ? next_tab : next_tab - tab_pos);
        if (val == "\\N") {
          row.push_back("");
        } else {
          row.push_back(val);
        }
        if (next_tab == std::string::npos) break;
        tab_pos = next_tab + 1;
      }
      result.rows.push_back(row);
      result.affected_rows++;
      continue;
    }
  }

  return result;
}

ProtoQueryResult GoodsDbProtocol::ParseOkError(const std::string& line) {
  ProtoQueryResult result;
  if (line.rfind("ERR ", 0) == 0) {
    result.is_error = true;
    result.error_message = line.substr(4);
  } else if (line.rfind("OK ", 0) == 0) {
    // Format: "OK affected_rows last_insert_id [info]"
    std::string rest = line.substr(3);
    size_t s1 = rest.find(' ');
    if (s1 != std::string::npos) {
      result.affected_rows = std::stoull(rest.substr(0, s1));
      size_t s2 = rest.find(' ', s1 + 1);
      if (s2 != std::string::npos) {
        result.last_insert_id = std::stoull(rest.substr(s1 + 1, s2 - s1 - 1));
      } else {
        result.last_insert_id = std::stoull(rest.substr(s1 + 1));
      }
    }
  }
  return result;
}

}  // namespace studio
}  // namespace goods_db
