#include "portforwardx/net/forwarder.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "net/forward_session.hpp"
#include "net/socket_platform.hpp"
#include "portforwardx/net/dns_resolver.hpp"

namespace portforwardx::net {

namespace {

int ToNativeFamily(AddressFamilyPreference family) {
  switch (family) {
    case AddressFamilyPreference::kIPv4Only:
      return AF_INET;
    case AddressFamilyPreference::kIPv6Only:
      return AF_INET6;
    case AddressFamilyPreference::kAuto:
    default:
      return AF_UNSPEC;
  }
}

bool IsIpv6Endpoint(const ResolvedEndpoint& endpoint) {
  return endpoint.family == AF_INET6;
}

int NativeSocketType(TransportProtocol protocol) {
  return protocol == TransportProtocol::kUdp ? SOCK_DGRAM : SOCK_STREAM;
}

int NativeSocketProtocol(TransportProtocol protocol) {
  return protocol == TransportProtocol::kUdp ? IPPROTO_UDP : IPPROTO_TCP;
}

std::vector<ResolvedEndpoint> SortEndpoints(std::vector<ResolvedEndpoint> endpoints,
                                            AddressFamilyPreference family) {
  if (family == AddressFamilyPreference::kAuto) {
    std::stable_sort(endpoints.begin(), endpoints.end(),
                     [](const ResolvedEndpoint& lhs, const ResolvedEndpoint& rhs) {
                       return lhs.family == AF_INET6 && rhs.family == AF_INET;
                     });
    return endpoints;
  }

  std::vector<ResolvedEndpoint> filtered;
  const int native = ToNativeFamily(family);
  for (auto& endpoint : endpoints) {
    if (endpoint.family == native) {
      filtered.push_back(std::move(endpoint));
    }
  }
  return filtered;
}

bool ConnectToEndpoints(const std::vector<ResolvedEndpoint>& endpoints,
                        TransportProtocol protocol,
                        SocketHandle* socket_out,
                        std::string* error_message) {
  for (const auto& endpoint : endpoints) {
    SocketHandle candidate = ::socket(endpoint.family, NativeSocketType(protocol),
                                      NativeSocketProtocol(protocol));  // NOLINT
    if (candidate == kInvalidSocket) {
      continue;
    }
    const auto* addr =
        reinterpret_cast<const sockaddr*>(endpoint.sockaddr_bytes.data());
    const auto addr_len = static_cast<socklen_t>(endpoint.sockaddr_len);
    if (connect(candidate, addr, addr_len) == 0) {
      *socket_out = candidate;
      return true;
    }
    detail::CloseSocket(candidate);
  }
  if (error_message != nullptr) {
    *error_message = "Unable to connect to target endpoint(s).";
  }
  return false;
}

}  // namespace

class Forwarder::Impl {
 public:
  explicit Impl(ForwardConfig config) : config_(std::move(config)) {}

  bool Start(std::string* error_message) {
    if (!detail::EnsureSocketApiInitialized(error_message)) {
      return false;
    }

    if (!ResolveTargetEndpoints(error_message)) {
      return false;
    }

    auto listen_endpoints =
        DnsResolver::Resolve(config_.listen_host, config_.listen_port, config_.protocol,
                             config_.family, true, error_message);
    listen_endpoints = SortEndpoints(std::move(listen_endpoints), config_.family);
    if (listen_endpoints.empty()) {
      if (error_message != nullptr && error_message->empty()) {
        *error_message = "No listen endpoint available.";
      }
      return false;
    }

    for (const auto& endpoint : listen_endpoints) {
      SocketHandle candidate =
          ::socket(endpoint.family, NativeSocketType(config_.protocol),
                   NativeSocketProtocol(config_.protocol));  // NOLINT
      if (candidate == kInvalidSocket) {
        continue;
      }

      std::string local_error;
      if (!detail::SetReuseAddress(candidate, &local_error)) {
        detail::CloseSocket(candidate);
        continue;
      }

      if (IsIpv6Endpoint(endpoint) && config_.family == AddressFamilyPreference::kAuto) {
        detail::SetIpv6Only(candidate, false, nullptr);
      }

      const auto* addr =
          reinterpret_cast<const sockaddr*>(endpoint.sockaddr_bytes.data());
      const auto addr_len = static_cast<socklen_t>(endpoint.sockaddr_len);

      if (bind(candidate, addr, addr_len) != 0) {
        detail::CloseSocket(candidate);
        continue;
      }
      if (config_.protocol == TransportProtocol::kTcp &&
          listen(candidate, config_.backlog) != 0) {
        detail::CloseSocket(candidate);
        continue;
      }
      if (config_.protocol == TransportProtocol::kUdp) {
#ifdef _WIN32
        DWORD timeout_ms = 200;
        setsockopt(candidate, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 200 * 1000;
        setsockopt(candidate, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
      }

      sockaddr_storage actual{};
      socklen_t actual_len = static_cast<socklen_t>(sizeof(actual));
      if (getsockname(candidate, reinterpret_cast<sockaddr*>(&actual), &actual_len) == 0) {
        if (actual.ss_family == AF_INET) {
          const auto* addr4 = reinterpret_cast<const sockaddr_in*>(&actual);
          listening_port_ = ntohs(addr4->sin_port);
          char ip[INET_ADDRSTRLEN] = {};
          if (inet_ntop(AF_INET, &addr4->sin_addr, ip, sizeof(ip)) != nullptr) {
            listening_host_ = ip;
          }
        } else if (actual.ss_family == AF_INET6) {
          const auto* addr6 = reinterpret_cast<const sockaddr_in6*>(&actual);
          listening_port_ = ntohs(addr6->sin6_port);
          char ip[INET6_ADDRSTRLEN] = {};
          if (inet_ntop(AF_INET6, &addr6->sin6_addr, ip, sizeof(ip)) != nullptr) {
            listening_host_ = ip;
          }
        }
      }

      listen_socket_ = candidate;
      running_.store(true);
      resolver_thread_ = std::thread(&Impl::ResolverLoop, this);
      if (config_.protocol == TransportProtocol::kTcp) {
        accept_thread_ = std::thread(&Impl::AcceptLoop, this);
      } else {
        udp_thread_ = std::thread(&Impl::UdpLoop, this);
      }
      return true;
    }

    if (error_message != nullptr && error_message->empty()) {
      *error_message = "Failed to bind/listen on any resolved endpoint.";
    }
    return false;
  }

  void Stop() {
    if (!running_.exchange(false)) {
      return;
    }

    detail::ShutdownBoth(listen_socket_);
    detail::CloseSocket(listen_socket_);
    listen_socket_ = kInvalidSocket;

    if (accept_thread_.joinable()) {
      accept_thread_.join();
    }
    if (udp_thread_.joinable()) {
      udp_thread_.join();
    }
    if (resolver_thread_.joinable()) {
      resolver_thread_.join();
    }

    std::vector<std::shared_ptr<detail::ForwardSession>> sessions;
    {
      std::lock_guard<std::mutex> lock(session_mutex_);
      for (auto& [id, session] : sessions_) {
        (void)id;
        sessions.push_back(session);
      }
    }

    for (const auto& session : sessions) {
      session->Stop();
    }
    for (const auto& session : sessions) {
      session->Join();
    }

    std::lock_guard<std::mutex> lock(session_mutex_);
    sessions_.clear();
  }

  bool IsRunning() const { return running_.load(); }

  std::uint16_t ListeningPort() const { return listening_port_; }
  std::string ListeningHost() const { return listening_host_; }

 private:
  bool ResolveTargetEndpoints(std::string* error_message) {
    std::string local_error;
    auto endpoints =
        DnsResolver::Resolve(config_.target_host, config_.target_port, config_.protocol,
                             config_.family, false, &local_error);
    endpoints = SortEndpoints(std::move(endpoints), config_.family);
    if (endpoints.empty()) {
      if (error_message != nullptr) {
        *error_message = local_error.empty() ? "No target endpoint available." : local_error;
      }
      return false;
    }
    std::lock_guard<std::mutex> lock(target_endpoint_mutex_);
    target_endpoints_ = std::move(endpoints);
    return true;
  }

  std::vector<ResolvedEndpoint> TargetEndpointsSnapshot() const {
    std::lock_guard<std::mutex> lock(target_endpoint_mutex_);
    return target_endpoints_;
  }

  void ResolverLoop() {
    const auto refresh_interval =
        std::chrono::milliseconds((config_.dns_refresh_interval_seconds == 0
                                       ? 30
                                       : config_.dns_refresh_interval_seconds) *
                                  1000);
    auto elapsed = std::chrono::milliseconds(0);
    while (running_.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      if (!running_.load()) {
        break;
      }
      elapsed += std::chrono::milliseconds(200);
      if (elapsed < refresh_interval) {
        continue;
      }
      elapsed = std::chrono::milliseconds(0);
      std::string ignore_error;
      ResolveTargetEndpoints(&ignore_error);
    }
  }

  void UdpLoop() {
    std::array<std::uint8_t, 64 * 1024> buffer{};
    while (running_.load()) {
      sockaddr_storage client_addr{};
      socklen_t client_len = static_cast<socklen_t>(sizeof(client_addr));
#ifdef _WIN32
      const int received =
          recvfrom(listen_socket_, reinterpret_cast<char*>(buffer.data()),
                   static_cast<int>(buffer.size()), 0, reinterpret_cast<sockaddr*>(&client_addr),
                   &client_len);
#else
      const ssize_t received =
          recvfrom(listen_socket_, buffer.data(), buffer.size(), 0,
                   reinterpret_cast<sockaddr*>(&client_addr), &client_len);
#endif
      if (received <= 0) {
        if (running_.load()) {
          continue;
        }
        break;
      }

      const auto endpoints = TargetEndpointsSnapshot();
      if (endpoints.empty()) {
        continue;
      }

      SocketHandle upstream = kInvalidSocket;
      std::string connect_error;
      if (!ConnectToEndpoints(endpoints, TransportProtocol::kUdp, &upstream, &connect_error)) {
        continue;
      }

      const auto sent =
          detail::Send(upstream, buffer.data(), static_cast<std::size_t>(received));
      if (sent <= 0) {
        detail::CloseSocket(upstream);
        continue;
      }

      // Request-response style UDP relay for local proxying.
#ifdef _WIN32
      DWORD timeout_ms = 500;
      setsockopt(upstream, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms),
                 sizeof(timeout_ms));
#else
      timeval timeout{};
      timeout.tv_sec = 0;
      timeout.tv_usec = 500 * 1000;
      setsockopt(upstream, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

      const auto reply = detail::Recv(upstream, buffer.data(), buffer.size());
      if (reply > 0) {
#ifdef _WIN32
        sendto(listen_socket_, reinterpret_cast<const char*>(buffer.data()),
               static_cast<int>(reply), 0, reinterpret_cast<const sockaddr*>(&client_addr),
               client_len);
#else
        sendto(listen_socket_, buffer.data(), static_cast<std::size_t>(reply), 0,
               reinterpret_cast<const sockaddr*>(&client_addr), client_len);
#endif
      }
      detail::CloseSocket(upstream);
    }
  }

  void AcceptLoop() {
    while (running_.load()) {
      CleanupFinishedSessions();

      sockaddr_storage peer{};
      socklen_t peer_len = static_cast<socklen_t>(sizeof(peer));
      SocketHandle client =
          accept(listen_socket_, reinterpret_cast<sockaddr*>(&peer), &peer_len);  // NOLINT
      if (client == kInvalidSocket) {
        if (running_.load()) {
          continue;
        }
        break;
      }

      auto target_endpoints = TargetEndpointsSnapshot();
      if (target_endpoints.empty()) {
        std::string refresh_error;
        ResolveTargetEndpoints(&refresh_error);
        target_endpoints = TargetEndpointsSnapshot();
      }
      if (target_endpoints.empty()) {
        detail::CloseSocket(client);
        continue;
      }

      SocketHandle upstream = kInvalidSocket;
      std::string connect_error;
      if (!ConnectToEndpoints(target_endpoints, TransportProtocol::kTcp, &upstream,
                              &connect_error)) {
        detail::CloseSocket(client);
        continue;
      }

      std::shared_ptr<detail::ForwardSession> session;
      std::uint64_t session_id = 0;
      {
        std::lock_guard<std::mutex> lock(session_mutex_);
        session_id = ++next_session_id_;
        session = std::make_shared<detail::ForwardSession>(client, upstream);
        sessions_[session_id] = session;
      }
      session->Start();
    }

    CleanupFinishedSessions();
  }

  void CleanupFinishedSessions() {
    std::vector<std::shared_ptr<detail::ForwardSession>> finished_sessions;
    {
      std::lock_guard<std::mutex> lock(session_mutex_);
      for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->second != nullptr && it->second->IsFinished()) {
          finished_sessions.push_back(it->second);
          it = sessions_.erase(it);
        } else {
          ++it;
        }
      }
    }
    for (const auto& session : finished_sessions) {
      session->Join();
    }
  }

  ForwardConfig config_;
  std::atomic<bool> running_{false};
  SocketHandle listen_socket_{kInvalidSocket};
  std::thread accept_thread_;
  std::thread udp_thread_;
  std::thread resolver_thread_;
  std::uint16_t listening_port_{0};
  std::string listening_host_;

  mutable std::mutex target_endpoint_mutex_;
  std::vector<ResolvedEndpoint> target_endpoints_;

  mutable std::mutex session_mutex_;
  std::unordered_map<std::uint64_t, std::shared_ptr<detail::ForwardSession>> sessions_;
  std::uint64_t next_session_id_{0};
};

Forwarder::Forwarder(ForwardConfig config) : config_(std::move(config)), impl_(new Impl(config_)) {}

Forwarder::~Forwarder() {
  Stop();
  delete impl_;
  impl_ = nullptr;
}

bool Forwarder::Start(std::string* error_message) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (running_.load()) {
    if (error_message != nullptr) {
      *error_message = "Forwarder already running.";
    }
    return false;
  }
  if (!impl_->Start(error_message)) {
    return false;
  }
  running_.store(true);
  listening_port_ = impl_->ListeningPort();
  listening_host_ = impl_->ListeningHost();
  return true;
}

void Forwarder::Stop() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (!running_.load()) {
    return;
  }
  impl_->Stop();
  running_.store(false);
}

bool Forwarder::IsRunning() const { return running_.load(); }

std::uint16_t Forwarder::ListeningPort() const { return listening_port_; }

std::string Forwarder::ListeningHost() const { return listening_host_; }

AddressFamilyPreference ParseAddressFamily(const std::string& input) {
  std::string lowered = input;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lowered == "ipv4" || lowered == "v4") {
    return AddressFamilyPreference::kIPv4Only;
  }
  if (lowered == "ipv6" || lowered == "v6") {
    return AddressFamilyPreference::kIPv6Only;
  }
  return AddressFamilyPreference::kAuto;
}

std::string AddressFamilyToString(AddressFamilyPreference family) {
  switch (family) {
    case AddressFamilyPreference::kIPv4Only:
      return "ipv4";
    case AddressFamilyPreference::kIPv6Only:
      return "ipv6";
    case AddressFamilyPreference::kAuto:
    default:
      return "auto";
  }
}

TransportProtocol ParseTransportProtocol(const std::string& input) {
  std::string lowered = input;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lowered == "udp") {
    return TransportProtocol::kUdp;
  }
  return TransportProtocol::kTcp;
}

std::string TransportProtocolToString(TransportProtocol protocol) {
  return protocol == TransportProtocol::kUdp ? "udp" : "tcp";
}

}  // namespace portforwardx::net
