#include "sql/network/connection.h"

#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace goods_db {

// Static counter definitions
std::atomic<uint64_t> Connection::total_bytes_sent_{0};
std::atomic<uint64_t> Connection::total_bytes_received_{0};
std::atomic<uint64_t> Connection::total_connections_{0};
std::atomic<uint64_t> Connection::active_connections_{0};

// Static getters
uint64_t Connection::GetTotalBytesSent() { return total_bytes_sent_.load(); }
uint64_t Connection::GetTotalBytesReceived() { return total_bytes_received_.load(); }
uint64_t Connection::GetTotalConnections() { return total_connections_.load(); }
uint64_t Connection::GetActiveConnectionCount() { return active_connections_.load(); }

Connection::Connection(int client_fd, const std::string& remote_addr,
                       uint16_t remote_port)
    : fd_(client_fd),
      remote_addr_(remote_addr),
      remote_port_(remote_port) {
  total_connections_++;
  active_connections_++;
}

Connection::~Connection() {
  Close();
}

void Connection::Close() {
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
    active_connections_--;
  }
  state_ = State::CLOSING;
}

bool Connection::ReadPacket(std::string& payload) {
  if (fd_ < 0) {
    return false;
  }

  // Read 4-byte header
  uint8_t header[4];
  size_t bytes_read = 0;
  while (bytes_read < 4) {
    ssize_t n = read(fd_, header + bytes_read, 4 - bytes_read);
    if (n <= 0) {
      if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
        return false;  // EOF or error
      }
      continue;  // EAGAIN or EINTR, retry
    }
    bytes_read += static_cast<size_t>(n);
  }

  bytes_received_ += 4;
  total_bytes_received_ += 4;

  uint32_t payload_len = ReadPacketLength(header);
  uint8_t seq_id = header[3];
  (void)seq_id;  // Sequence ID validation can be added here

  if (payload_len > kMaxPayloadLength) {
    return false;  // Packet too large
  }

  // Read payload
  payload.resize(payload_len);
  bytes_read = 0;
  while (bytes_read < payload_len) {
    ssize_t n = read(fd_, &payload[bytes_read], payload_len - bytes_read);
    if (n <= 0) {
      if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
        return false;
      }
      continue;
    }
    bytes_read += static_cast<size_t>(n);
  }

  bytes_received_ += payload_len;
  total_bytes_received_ += payload_len;

  return true;
}

bool Connection::WritePacket(const uint8_t* data, size_t len) {
  if (fd_ < 0) {
    return false;
  }

  // Build header + payload
  uint8_t header[4];
  WritePacketLength(header, static_cast<uint32_t>(len));
  header[3] = 0;  // sequence_id — caller's Protocol handles this

  // Write header
  size_t written = 0;
  while (written < 4) {
    ssize_t n = write(fd_, header + written, 4 - written);
    if (n <= 0) {
      if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
      return false;
    }
    written += static_cast<size_t>(n);
  }

  bytes_sent_ += 4;
  total_bytes_sent_ += 4;

  // Write payload
  written = 0;
  while (written < len) {
    ssize_t n = write(fd_, data + written, len - written);
    if (n <= 0) {
      if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
      return false;
    }
    written += static_cast<size_t>(n);
  }

  bytes_sent_ += len;
  total_bytes_sent_ += len;

  return true;
}

bool Connection::WritePacketStr(const std::string& data) {
  return WritePacket(reinterpret_cast<const uint8_t*>(data.data()),
                     data.size());
}

void Connection::SetAuthenticated(bool auth, const std::string& user,
                                   const std::string& db) {
  authenticated_ = auth;
  if (auth) {
    user_ = user;
    db_ = db;
  }
}

}  // namespace goods_db
