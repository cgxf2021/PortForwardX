#pragma once

#include <atomic>
#include <mutex>
#include <thread>

#include "net/socket_platform.hpp"

namespace portforwardx::net::detail {

class ForwardSession {
 public:
  ForwardSession(SocketHandle client_socket, SocketHandle upstream_socket);
  ~ForwardSession();

  ForwardSession(const ForwardSession&) = delete;
  ForwardSession& operator=(const ForwardSession&) = delete;

  void Start();
  void Stop();
  void Join();
  bool IsFinished() const;

 private:
  void Run();
  void CloseSockets();

  SocketHandle client_socket_;
  SocketHandle upstream_socket_;
  std::thread worker_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> finished_{false};
  std::mutex socket_mutex_;
};

}  // namespace portforwardx::net::detail
