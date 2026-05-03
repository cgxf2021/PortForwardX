#include "net/forward_session.hpp"

#include <array>
#include <utility>

namespace portforwardx::net::detail {

namespace {

bool SendAll(SocketHandle destination, const std::uint8_t* data, std::size_t size) {
  std::size_t sent = 0;
  while (sent < size) {
    const auto chunk = Send(destination, data + sent, size - sent);
    if (chunk <= 0) {
      return false;
    }
    sent += static_cast<std::size_t>(chunk);
  }
  return true;
}

void RelayOneDirection(SocketHandle source, SocketHandle destination) {
  std::array<std::uint8_t, 16 * 1024> buffer{};
  while (true) {
    const auto received = Recv(source, buffer.data(), buffer.size());
    if (received == 0) {
      ShutdownWrite(destination);
      return;
    }
    if (received < 0) {
      ShutdownBoth(destination);
      return;
    }
    if (!SendAll(destination, buffer.data(), static_cast<std::size_t>(received))) {
      ShutdownBoth(source);
      ShutdownBoth(destination);
      return;
    }
  }
}

}  // namespace

ForwardSession::ForwardSession(SocketHandle client_socket, SocketHandle upstream_socket)
    : client_socket_(client_socket), upstream_socket_(upstream_socket) {}

ForwardSession::~ForwardSession() {
  Stop();
  Join();
}

void ForwardSession::Start() { worker_ = std::thread(&ForwardSession::Run, this); }

void ForwardSession::Stop() {
  if (stopping_.exchange(true)) {
    return;
  }
  std::lock_guard<std::mutex> lock(socket_mutex_);
  ShutdownBoth(client_socket_);
  ShutdownBoth(upstream_socket_);
}

void ForwardSession::Join() {
  if (worker_.joinable()) {
    if (worker_.get_id() == std::this_thread::get_id()) {
      worker_.detach();
      return;
    }
    worker_.join();
  }
}

bool ForwardSession::IsFinished() const { return finished_.load(); }

void ForwardSession::Run() {
  std::thread upstream([this] { RelayOneDirection(client_socket_, upstream_socket_); });
  std::thread downstream([this] { RelayOneDirection(upstream_socket_, client_socket_); });
  upstream.join();
  downstream.join();

  CloseSockets();
  finished_.store(true);
}

void ForwardSession::CloseSockets() {
  std::lock_guard<std::mutex> lock(socket_mutex_);
  if (client_socket_ != kInvalidSocket) {
    CloseSocket(client_socket_);
    client_socket_ = kInvalidSocket;
  }
  if (upstream_socket_ != kInvalidSocket) {
    CloseSocket(upstream_socket_);
    upstream_socket_ = kInvalidSocket;
  }
}

}  // namespace portforwardx::net::detail
