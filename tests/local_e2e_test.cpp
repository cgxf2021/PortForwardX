#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

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
#include <netdb.h>
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

bool SendAll(TestSocket socket, const std::string& payload) {
  std::size_t written = 0;
  while (written < payload.size()) {
#ifdef _WIN32
    const int rc = send(socket, payload.data() + written,
                        static_cast<int>(payload.size() - written), 0);
#else
    const ssize_t rc = send(socket, payload.data() + written, payload.size() - written, 0);
#endif
    if (rc <= 0) {
      return false;
    }
    written += static_cast<std::size_t>(rc);
  }
  return true;
}

bool RecvExactly(TestSocket socket, std::string* payload) {
  std::string output(payload->size(), '\0');
  std::size_t read_len = 0;
  while (read_len < output.size()) {
#ifdef _WIN32
    const int rc =
        recv(socket, output.data() + read_len, static_cast<int>(output.size() - read_len), 0);
#else
    const ssize_t rc = recv(socket, output.data() + read_len, output.size() - read_len, 0);
#endif
    if (rc <= 0) {
      return false;
    }
    read_len += static_cast<std::size_t>(rc);
  }
  *payload = std::move(output);
  return true;
}

class EchoServer {
 public:
  bool Start(std::string* error_message) {
    listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);  // NOLINT
    if (listener_ == kInvalidTestSocket) {
      *error_message = "echo: socket create failed";
      return false;
    }

    int reuse = 1;
    setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
               static_cast<socklen_t>(sizeof(reuse)));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(listener_, reinterpret_cast<const sockaddr*>(&addr),
             static_cast<socklen_t>(sizeof(addr))) != 0) {
      *error_message = "echo: bind failed";
      return false;
    }
    if (listen(listener_, 64) != 0) {
      *error_message = "echo: listen failed";
      return false;
    }

    sockaddr_storage actual{};
    socklen_t len = static_cast<socklen_t>(sizeof(actual));
    if (getsockname(listener_, reinterpret_cast<sockaddr*>(&actual), &len) != 0) {
      *error_message = "echo: getsockname failed";
      return false;
    }
    const auto* a4 = reinterpret_cast<const sockaddr_in*>(&actual);
    port_ = ntohs(a4->sin_port);

    running_.store(true);
    worker_ = std::thread([this] { Serve(); });
    return true;
  }

  void Stop() {
    if (!running_.exchange(false)) {
      return;
    }
#ifdef _WIN32
    shutdown(listener_, SD_BOTH);
#else
    shutdown(listener_, SHUT_RDWR);
#endif
    CloseSocket(listener_);
    listener_ = kInvalidTestSocket;
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  ~EchoServer() { Stop(); }
  std::uint16_t port() const { return port_; }

 private:
  void ServeClient(TestSocket client) {
    std::array<char, 16 * 1024> buffer{};
    while (true) {
#ifdef _WIN32
      const int rc = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
      const ssize_t rc = recv(client, buffer.data(), buffer.size(), 0);
#endif
      if (rc <= 0) {
        break;
      }

      std::size_t sent = 0;
      while (sent < static_cast<std::size_t>(rc)) {
#ifdef _WIN32
        const int wc = send(client, buffer.data() + sent,
                            static_cast<int>(static_cast<std::size_t>(rc) - sent), 0);
#else
        const ssize_t wc =
            send(client, buffer.data() + sent, static_cast<std::size_t>(rc) - sent, 0);
#endif
        if (wc <= 0) {
          break;
        }
        sent += static_cast<std::size_t>(wc);
      }
      if (sent < static_cast<std::size_t>(rc)) {
        break;
      }
    }
    CloseSocket(client);
  }

  void Serve() {
    while (running_.load()) {
      sockaddr_storage peer{};
      socklen_t len = static_cast<socklen_t>(sizeof(peer));
      TestSocket client = accept(listener_, reinterpret_cast<sockaddr*>(&peer), &len);  // NOLINT
      if (client == kInvalidTestSocket) {
        if (!running_.load()) {
          break;
        }
        continue;
      }
      client_workers_.emplace_back([this, client] { ServeClient(client); });
    }

    for (auto& worker : client_workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
    client_workers_.clear();
  }

  std::atomic<bool> running_{false};
  TestSocket listener_{kInvalidTestSocket};
  std::thread worker_;
  std::vector<std::thread> client_workers_;
  std::uint16_t port_{0};
};

bool ConnectWithRetry(const std::string& host, std::uint16_t port, int retry_count,
                      int retry_sleep_ms, TestSocket* out_socket) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  for (int retry = 0; retry < retry_count; ++retry) {
    addrinfo* results = nullptr;
    const int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &results);
    if (rc != 0 || results == nullptr) {
      std::this_thread::sleep_for(std::chrono::milliseconds(retry_sleep_ms));
      continue;
    }

    for (addrinfo* it = results; it != nullptr; it = it->ai_next) {
      TestSocket candidate = socket(it->ai_family, SOCK_STREAM, IPPROTO_TCP);  // NOLINT
      if (candidate == kInvalidTestSocket) {
        continue;
      }
      if (connect(candidate, it->ai_addr, static_cast<socklen_t>(it->ai_addrlen)) == 0) {
        freeaddrinfo(results);
        *out_socket = candidate;
        return true;
      }
      CloseSocket(candidate);
    }

    freeaddrinfo(results);
    std::this_thread::sleep_for(std::chrono::milliseconds(retry_sleep_ms));
  }
  return false;
}

std::string BuildPayload(int client_id, int iter) {
  return "client=" + std::to_string(client_id) + ";iter=" + std::to_string(iter) +
         ";payload=abcdefghijklmnopqrstuvwxyz0123456789";
}

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
  EchoServer echo;
  if (!echo.Start(&error)) {
    std::cerr << "[FAIL] " << error << "\n";
    return 1;
  }

  portforwardx::net::ForwardConfig cfg;
  cfg.listen_host = "127.0.0.1";
  cfg.listen_port = 0;
  cfg.target_host = "localhost";  // Ensure domain resolution is exercised.
  cfg.target_port = echo.port();
  cfg.family = portforwardx::net::AddressFamilyPreference::kAuto;

  portforwardx::net::Forwarder forwarder(cfg);
  if (!forwarder.Start(&error)) {
    echo.Stop();
    std::cerr << "[FAIL] forwarder start failed: " << error << "\n";
    return 1;
  }

  constexpr int kClientCount = 6;
  constexpr int kMessagePerClient = 15;
  std::atomic<int> failure_count{0};
  std::vector<std::thread> clients;
  clients.reserve(kClientCount);

  for (int client_id = 0; client_id < kClientCount; ++client_id) {
    clients.emplace_back([client_id, &forwarder, &failure_count] {
      TestSocket client = kInvalidTestSocket;
      if (!ConnectWithRetry("127.0.0.1", forwarder.ListeningPort(), 30, 50, &client)) {
        ++failure_count;
        return;
      }

      for (int i = 0; i < kMessagePerClient; ++i) {
        const std::string payload = BuildPayload(client_id, i);
        std::string echoed(payload.size(), '\0');
        if (!SendAll(client, payload) || !RecvExactly(client, &echoed) || echoed != payload) {
          ++failure_count;
          break;
        }
      }

      CloseSocket(client);
    });
  }

  for (auto& client : clients) {
    if (client.joinable()) {
      client.join();
    }
  }

  forwarder.Stop();
  echo.Stop();

  if (failure_count.load() > 0) {
    std::cerr << "[FAIL] local e2e test failures: " << failure_count.load() << "\n";
    return 1;
  }

  std::cout << "[PASS] local e2e test\n";
  return 0;
}
