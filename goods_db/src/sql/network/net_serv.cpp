#include "sql/network/net_serv.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace goods_db {

SocketServer::~SocketServer() {
  Close();
}

int SocketServer::CreateSocket() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }

  // Set SO_REUSEADDR to allow quick restart
  int opt = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  return fd;
}

bool SocketServer::BindSocket(int fd, const std::string& host,
                               uint16_t port) {
  struct sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  if (host == "0.0.0.0" || host == "*") {
    addr.sin_addr.s_addr = INADDR_ANY;
  } else {
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
      return false;
    }
  }

  return bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0;
}

void SocketServer::SetNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
}

bool SocketServer::Listen(const std::string& host, uint16_t port,
                           int backlog) {
  listen_fd_ = CreateSocket();
  if (listen_fd_ < 0) {
    return false;
  }

  if (!BindSocket(listen_fd_, host, port)) {
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  if (listen(listen_fd_, backlog) < 0) {
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  return true;
}

int SocketServer::Accept() {
  if (listen_fd_ < 0) {
    return -1;
  }

  struct sockaddr_in client_addr;
  socklen_t addr_len = sizeof(client_addr);
  int client_fd = accept(listen_fd_,
                         reinterpret_cast<struct sockaddr*>(&client_addr),
                         &addr_len);
  return client_fd;
}

void SocketServer::Close() {
  if (listen_fd_ >= 0) {
    close(listen_fd_);
    listen_fd_ = -1;
  }
}

}  // namespace goods_db
