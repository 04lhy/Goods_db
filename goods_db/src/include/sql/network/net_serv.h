#pragma once

#include <cstdint>
#include <string>

namespace goods_db {

// =============================================================================
// SocketServer — TCP socket listener
//
// Creates a listening socket, accepts incoming client connections.
// =============================================================================
class SocketServer {
 public:
  SocketServer() = default;
  ~SocketServer();

  // Bind and listen on host:port. Returns false on failure.
  bool Listen(const std::string& host, uint16_t port, int backlog = 128);

  // Accept a new client connection. Returns client fd, or -1 on error.
  int Accept();

  // Close the listening socket.
  void Close();

  bool IsListening() const { return listen_fd_ >= 0; }
  int GetListenFd() const { return listen_fd_; }

  // ---- Static helpers -----------------------------------------------------
  static void SetNonBlocking(int fd);
  static int CreateSocket();
  static bool BindSocket(int fd, const std::string& host, uint16_t port);

 private:
  int listen_fd_ = -1;
};

}  // namespace goods_db
