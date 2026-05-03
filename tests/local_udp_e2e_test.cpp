#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using TestSocket = SOCKET;
constexpr TestSocket kInvalidTestSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using TestSocket = int;
constexpr TestSocket kInvalidTestSocket = -1;
#endif

#include "portforwardx/net/forwarder.hpp"

namespace {

void CloseSocket(TestSocket s) {
  if (s == kInvalidTestSocket) {
    return;
  }
#ifdef _WIN32
  closesocket(s);
#else
  close(s);
#endif
}

class UdpEchoServer {
 public:
  bool Start(std::string* error_message) {
    socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);  // NOLINT
    if (socket_ == kInvalidTestSocket) {
      *error_message = "udp echo: create socket failed";
      return false;
    }

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind_addr.sin_port = 0;
    if (bind(socket_, reinterpret_cast<const sockaddr*>(&bind_addr),
             static_cast<socklen_t>(sizeof(bind_addr))) != 0) {
      *error_message = "udp echo: bind failed";
      return false;
    }

#ifdef _WIN32
    DWORD timeout_ms = 200;
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms),
               sizeof(timeout_ms));
#else
    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 200 * 1000;
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

    sockaddr_storage actual{};
    socklen_t actual_len = static_cast<socklen_t>(sizeof(actual));
    if (getsockname(socket_, reinterpret_cast<sockaddr*>(&actual), &actual_len) != 0) {
      *error_message = "udp echo: getsockname failed";
      return false;
    }
    const auto* addr4 = reinterpret_cast<const sockaddr_in*>(&actual);
    port_ = ntohs(addr4->sin_port);

    running_.store(true);
    worker_ = std::thread([this] { Loop(); });
    return true;
  }

  void Stop() {
    if (!running_.exchange(false)) {
      return;
    }
    CloseSocket(socket_);
    socket_ = kInvalidTestSocket;
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  ~UdpEchoServer() { Stop(); }

  std::uint16_t port() const { return port_; }

 private:
  void Loop() {
    std::array<std::uint8_t, 4096> buffer{};
    while (running_.load()) {
      sockaddr_storage peer{};
      socklen_t peer_len = static_cast<socklen_t>(sizeof(peer));
#ifdef _WIN32
      const int received =
          recvfrom(socket_, reinterpret_cast<char*>(buffer.data()),
                   static_cast<int>(buffer.size()), 0, reinterpret_cast<sockaddr*>(&peer),
                   &peer_len);
#else
      const ssize_t received =
          recvfrom(socket_, buffer.data(), buffer.size(), 0,
                   reinterpret_cast<sockaddr*>(&peer), &peer_len);
#endif
      if (received <= 0) {
        if (!running_.load()) {
          break;
        }
        continue;
      }
#ifdef _WIN32
      sendto(socket_, reinterpret_cast<const char*>(buffer.data()), static_cast<int>(received), 0,
             reinterpret_cast<const sockaddr*>(&peer), peer_len);
#else
      sendto(socket_, buffer.data(), static_cast<std::size_t>(received), 0,
             reinterpret_cast<const sockaddr*>(&peer), peer_len);
#endif
    }
  }

  std::atomic<bool> running_{false};
  TestSocket socket_{kInvalidTestSocket};
  std::thread worker_;
  std::uint16_t port_{0};
};

}  // namespace

int main() {
#ifdef _WIN32
  struct WsaGuard {
    ~WsaGuard() { WSACleanup(); }
  };
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    std::cerr << "[FAIL] WSAStartup failed\n";
    return 1;
  }
  WsaGuard guard;
#endif

  std::string error;
  UdpEchoServer echo;
  if (!echo.Start(&error)) {
    std::cerr << "[FAIL] " << error << "\n";
    return 1;
  }

  portforwardx::net::ForwardConfig cfg;
  cfg.listen_host = "127.0.0.1";
  cfg.listen_port = 0;
  cfg.target_host = "localhost";
  cfg.target_port = echo.port();
  cfg.protocol = portforwardx::net::TransportProtocol::kUdp;
  cfg.family = portforwardx::net::AddressFamilyPreference::kIPv4Only;
  cfg.dns_refresh_interval_seconds = 1;

  portforwardx::net::Forwarder forwarder(cfg);
  if (!forwarder.Start(&error)) {
    echo.Stop();
    std::cerr << "[FAIL] forwarder start failed: " << error << "\n";
    return 1;
  }

  TestSocket client = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);  // NOLINT
  if (client == kInvalidTestSocket) {
    forwarder.Stop();
    echo.Stop();
    std::cerr << "[FAIL] client socket create failed\n";
    return 1;
  }

  sockaddr_in target{};
  target.sin_family = AF_INET;
  target.sin_port = htons(forwarder.ListeningPort());
  inet_pton(AF_INET, "127.0.0.1", &target.sin_addr);

  const std::string payload = "udp-local-e2e";
#ifdef _WIN32
  const int sent = sendto(client, payload.data(), static_cast<int>(payload.size()), 0,
                          reinterpret_cast<const sockaddr*>(&target),
                          static_cast<socklen_t>(sizeof(target)));
#else
  const ssize_t sent = sendto(client, payload.data(), payload.size(), 0,
                              reinterpret_cast<const sockaddr*>(&target),
                              static_cast<socklen_t>(sizeof(target)));
#endif
  if (sent <= 0) {
    CloseSocket(client);
    forwarder.Stop();
    echo.Stop();
    std::cerr << "[FAIL] client send failed\n";
    return 1;
  }

#ifdef _WIN32
  DWORD timeout_ms = 1500;
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms),
             sizeof(timeout_ms));
#else
  timeval timeout{};
  timeout.tv_sec = 1;
  timeout.tv_usec = 500 * 1000;
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

  std::array<char, 256> reply{};
#ifdef _WIN32
  const int received = recv(client, reply.data(), static_cast<int>(reply.size()), 0);
#else
  const ssize_t received = recv(client, reply.data(), reply.size(), 0);
#endif
  CloseSocket(client);
  forwarder.Stop();
  echo.Stop();

  if (received <= 0) {
    std::cerr << "[FAIL] client recv failed\n";
    return 1;
  }
  const std::string echoed(reply.data(), static_cast<std::size_t>(received));
  if (echoed != payload) {
    std::cerr << "[FAIL] payload mismatch: " << echoed << "\n";
    return 1;
  }

  std::cout << "[PASS] local udp e2e test\n";
  return 0;
}
