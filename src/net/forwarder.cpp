#include "portforwardx/net/forwarder.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "net/forward_session.hpp"
#include "net/socket_platform.hpp"
#include "portforwardx/net/dns_resolver.hpp"

namespace portforwardx::net {

namespace {

constexpr int kSocketBufferSize = 2 * 1024 * 1024;

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

std::string EndpointToString(const sockaddr_storage& address, socklen_t address_len);

std::string ResolvedEndpointToString(const ResolvedEndpoint& endpoint) {
  std::ostringstream oss;
  if (endpoint.family == AF_INET6) {
    oss << "ipv6 [" << endpoint.ip << "]:" << endpoint.port;
  } else if (endpoint.family == AF_INET) {
    oss << "ipv4 " << endpoint.ip << ":" << endpoint.port;
  } else {
    oss << "unknown " << endpoint.ip << ":" << endpoint.port;
  }
  return oss.str();
}

std::string FormatResolvedEndpointList(const std::vector<ResolvedEndpoint>& endpoints) {
  if (endpoints.empty()) {
    return "(none)";
  }

  std::ostringstream oss;
  for (std::size_t i = 0; i < endpoints.size(); ++i) {
    if (i > 0) {
      oss << ", ";
    }
    oss << ResolvedEndpointToString(endpoints[i]);
  }
  return oss.str();
}

bool ConnectToEndpoints(const std::vector<ResolvedEndpoint>& endpoints,
                        TransportProtocol protocol,
                        SocketHandle* socket_out,
                        std::string* error_message) {
  int last_error = 0;
  for (const auto& endpoint : endpoints) {
    SocketHandle candidate = ::socket(endpoint.family, NativeSocketType(protocol),
                                      NativeSocketProtocol(protocol));  // NOLINT
    if (candidate == kInvalidSocket) {
      last_error = detail::LastSocketError();
      continue;
    }
    if (protocol == TransportProtocol::kUdp) {
      detail::SetSocketBufferSize(candidate, kSocketBufferSize, nullptr);
      detail::DisableUdpConnReset(candidate, nullptr);
    }
    const auto* addr =
        reinterpret_cast<const sockaddr*>(endpoint.sockaddr_bytes.data());
    const auto addr_len = static_cast<socklen_t>(endpoint.sockaddr_len);
    if (connect(candidate, addr, addr_len) == 0) {
      *socket_out = candidate;
      return true;
    }
    last_error = detail::LastSocketError();
    detail::CloseSocket(candidate);
  }
  if (error_message != nullptr) {
    *error_message = "Unable to connect to target endpoint(s)";
    if (last_error != 0) {
      *error_message += ", last_error=" + detail::SocketErrorToString(last_error);
    }
    *error_message += ".";
  }
  return false;
}

bool CreateUdpSocketForEndpoint(const std::vector<ResolvedEndpoint>& endpoints,
                                SocketHandle* socket_out,
                                sockaddr_storage* target_addr_out,
                                socklen_t* target_len_out,
                                std::string* target_label_out,
                                std::string* error_message) {
  int last_error = 0;
  for (const auto& endpoint : endpoints) {
    if (endpoint.sockaddr_len > sizeof(sockaddr_storage)) {
      continue;
    }
    SocketHandle candidate =
        ::socket(endpoint.family, SOCK_DGRAM, IPPROTO_UDP);  // NOLINT
    if (candidate == kInvalidSocket) {
      last_error = detail::LastSocketError();
      continue;
    }
    detail::SetSocketBufferSize(candidate, kSocketBufferSize, nullptr);
    detail::DisableUdpConnReset(candidate, nullptr);

    std::memset(target_addr_out, 0, sizeof(*target_addr_out));
    std::memcpy(target_addr_out, endpoint.sockaddr_bytes.data(), endpoint.sockaddr_len);
    *target_len_out = static_cast<socklen_t>(endpoint.sockaddr_len);
    if (target_label_out != nullptr) {
      *target_label_out = EndpointToString(*target_addr_out, *target_len_out);
    }
    *socket_out = candidate;
    return true;
  }
  if (error_message != nullptr) {
    *error_message = "Unable to create UDP socket for target endpoint(s)";
    if (last_error != 0) {
      *error_message += ", last_error=" + detail::SocketErrorToString(last_error);
    }
    *error_message += ".";
  }
  return false;
}

void SetUdpRecvTimeout(SocketHandle socket, int timeout_ms) {
#ifdef _WIN32
  const DWORD timeout = static_cast<DWORD>(timeout_ms);
  setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
             sizeof(timeout));
#else
  timeval timeout{};
  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;
  setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
}

bool IsTransientRecvError(int error_code) {
#ifdef _WIN32
  return error_code == WSAEWOULDBLOCK || error_code == WSAETIMEDOUT ||
         error_code == WSAEINTR;
#else
  return error_code == EAGAIN || error_code == EWOULDBLOCK || error_code == EINTR;
#endif
}

std::uint64_t NowMonotonicMs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

std::string EndpointToString(const sockaddr_storage& address, socklen_t address_len) {
  char host[NI_MAXHOST] = {};
  char service[NI_MAXSERV] = {};
  const int rc = getnameinfo(reinterpret_cast<const sockaddr*>(&address), address_len, host,
                             static_cast<socklen_t>(sizeof(host)), service,
                             static_cast<socklen_t>(sizeof(service)),
                             NI_NUMERICHOST | NI_NUMERICSERV);
  if (rc == 0) {
    return std::string(host) + ":" + service;
  }
  return "unknown";
}

std::string ClientEndpointKey(const sockaddr_storage& address, socklen_t address_len) {
  return EndpointToString(address, address_len);
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

    const auto listen_family = EffectiveListenFamily();
    auto listen_endpoints =
        DnsResolver::Resolve(config_.listen_host, config_.listen_port, config_.protocol,
                             listen_family, true, error_message);
    listen_endpoints = SortEndpoints(std::move(listen_endpoints), listen_family);
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

      if (IsIpv6Endpoint(endpoint) && listen_family == AddressFamilyPreference::kAuto) {
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
        detail::SetSocketBufferSize(candidate, kSocketBufferSize, nullptr);
        detail::DisableUdpConnReset(candidate, nullptr);
        SetUdpRecvTimeout(candidate, 200);
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

    CleanupIdleUdpSessions(true);

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
    session_peers_.clear();
  }

  bool IsRunning() const { return running_.load(); }

  std::uint16_t ListeningPort() const { return listening_port_; }
  std::string ListeningHost() const { return listening_host_; }

 private:
  AddressFamilyPreference EffectiveListenFamily() const {
    if (config_.listen_family != AddressFamilyPreference::kAuto) {
      return config_.listen_family;
    }
    if (config_.target_family == AddressFamilyPreference::kAuto &&
        config_.family != AddressFamilyPreference::kAuto) {
      return config_.family;
    }
    return AddressFamilyPreference::kAuto;
  }

  AddressFamilyPreference EffectiveTargetFamily() const {
    if (config_.target_family != AddressFamilyPreference::kAuto) {
      return config_.target_family;
    }
    if (config_.listen_family == AddressFamilyPreference::kAuto &&
        config_.family != AddressFamilyPreference::kAuto) {
      return config_.family;
    }
    return AddressFamilyPreference::kAuto;
  }

  bool ResolveTargetEndpoints(std::string* error_message) {
    std::string local_error;
    const auto target_family = EffectiveTargetFamily();
    auto endpoints =
        DnsResolver::Resolve(config_.target_host, config_.target_port, config_.protocol,
                             target_family, false, &local_error);
    endpoints = SortEndpoints(std::move(endpoints), target_family);
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

  std::size_t TargetEndpointCount() const {
    std::lock_guard<std::mutex> lock(target_endpoint_mutex_);
    return target_endpoints_.size();
  }

  void ResolverLoop() {
    const auto refresh_interval =
        std::chrono::milliseconds((config_.dns_refresh_interval_minutes == 0
                                       ? 10
                                       : config_.dns_refresh_interval_minutes) *
                                  60 * 1000);
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
      std::string refresh_error;
      if (ResolveTargetEndpoints(&refresh_error)) {
        const auto endpoints = TargetEndpointsSnapshot();
        std::cout << "[dns] refresh ok: " << config_.target_host << ":"
                  << config_.target_port << " -> "
                  << FormatResolvedEndpointList(endpoints) << "\n";
      } else {
        std::cout << "[dns] refresh failed for " << config_.target_host << ":"
                  << config_.target_port << ", reason=" << refresh_error << "\n";
      }
    }
  }

  void UdpLoop() {
    std::array<std::uint8_t, 64 * 1024> buffer{};
    last_udp_cleanup_ms_ = NowMonotonicMs();
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
          MaybeCleanupIdleUdpSessions();
          continue;
        }
        break;
      }

      const auto session = GetOrCreateUdpSession(client_addr, client_len);
      if (session == nullptr) {
        MaybeCleanupIdleUdpSessions();
        continue;
      }
      session->last_active_ms.store(NowMonotonicMs(), std::memory_order_relaxed);

#ifdef _WIN32
      const int sent = sendto(session->upstream_socket,
                              reinterpret_cast<const char*>(buffer.data()),
                              static_cast<int>(received), 0,
                              reinterpret_cast<const sockaddr*>(&session->target_addr),
                              session->target_len);
#else
      const ssize_t sent = sendto(session->upstream_socket, buffer.data(),
                                  static_cast<std::size_t>(received), 0,
                                  reinterpret_cast<const sockaddr*>(&session->target_addr),
                                  session->target_len);
#endif
      if (sent <= 0) {
        const int socket_error = detail::LastSocketError();
        std::cout << "[udp] client->target failed: client=" << session->client_label
                  << ", bytes=" << received
                  << ", reason=" << detail::SocketErrorToString(socket_error) << "\n";
        session->marked_for_close.store(true);
      } else {
        session->client_to_target_packets.fetch_add(1, std::memory_order_relaxed);
        session->client_to_target_bytes.fetch_add(static_cast<std::uint64_t>(sent),
                                                  std::memory_order_relaxed);
        if (config_.udp_debug) {
          std::cout << "[udp] client->target: client=" << session->client_label
                    << ", bytes=" << sent << ", target=" << session->target_label << "\n";
        }
      }

      MaybeCleanupIdleUdpSessions();
    }
    CleanupIdleUdpSessions(true);
  }

  struct UdpSession {
    SocketHandle upstream_socket{kInvalidSocket};
    sockaddr_storage target_addr{};
    socklen_t target_len = 0;
    sockaddr_storage client_addr{};
    socklen_t client_len = 0;
    std::string client_label;
    std::string target_label;
    std::thread upstream_to_client_thread;
    std::atomic<std::uint64_t> last_active_ms{0};
    std::atomic<std::uint64_t> client_to_target_packets{0};
    std::atomic<std::uint64_t> client_to_target_bytes{0};
    std::atomic<std::uint64_t> target_to_client_packets{0};
    std::atomic<std::uint64_t> target_to_client_bytes{0};
    std::atomic<bool> marked_for_close{false};
  };

  std::shared_ptr<UdpSession> GetOrCreateUdpSession(const sockaddr_storage& client_addr,
                                                    socklen_t client_len) {
    const auto key = ClientEndpointKey(client_addr, client_len);
    {
      std::lock_guard<std::mutex> lock(udp_session_mutex_);
      const auto it = udp_sessions_.find(key);
      if (it != udp_sessions_.end()) {
        return it->second;
      }
    }

    const auto endpoints = TargetEndpointsSnapshot();
    if (endpoints.empty()) {
      return nullptr;
    }

    SocketHandle upstream = kInvalidSocket;
    sockaddr_storage target_addr{};
    socklen_t target_len = 0;
    std::string target_label;
    std::string create_error;
    if (!CreateUdpSocketForEndpoint(endpoints, &upstream, &target_addr, &target_len,
                                    &target_label, &create_error)) {
      std::cout << "[udp] create upstream failed for client "
                << EndpointToString(client_addr, client_len) << ", reason=" << create_error
                << "\n";
      return nullptr;
    }
    SetUdpRecvTimeout(upstream, 200);

    auto session = std::make_shared<UdpSession>();
    session->upstream_socket = upstream;
    session->target_addr = target_addr;
    session->target_len = target_len;
    session->client_addr = client_addr;
    session->client_len = client_len;
    session->client_label = EndpointToString(client_addr, client_len);
    session->target_label = target_label;
    session->last_active_ms.store(NowMonotonicMs(), std::memory_order_relaxed);

    {
      std::lock_guard<std::mutex> lock(udp_session_mutex_);
      const auto [it, inserted] = udp_sessions_.emplace(key, session);
      if (!inserted) {
        detail::CloseSocket(upstream);
        return it->second;
      }
    }

    std::cout << "[udp] client connected: " << session->client_label << "\n";
    session->upstream_to_client_thread = std::thread(&Impl::UdpUpstreamToClientLoop, this, session);
    return session;
  }

  void UdpUpstreamToClientLoop(const std::shared_ptr<UdpSession>& session) {
    std::array<std::uint8_t, 64 * 1024> buffer{};
    while (running_.load() && !session->marked_for_close.load()) {
      sockaddr_storage remote_addr{};
      socklen_t remote_len = static_cast<socklen_t>(sizeof(remote_addr));
#ifdef _WIN32
      const int received =
          recvfrom(session->upstream_socket, reinterpret_cast<char*>(buffer.data()),
                   static_cast<int>(buffer.size()), 0,
                   reinterpret_cast<sockaddr*>(&remote_addr), &remote_len);
#else
      const ssize_t received =
          recvfrom(session->upstream_socket, buffer.data(), buffer.size(), 0,
                   reinterpret_cast<sockaddr*>(&remote_addr), &remote_len);
#endif
      if (received <= 0) {
        if (received < 0) {
          const int socket_error = detail::LastSocketError();
          if (IsTransientRecvError(socket_error)) {
            continue;
          }
          std::cout << "[udp] target->client recv failed: client="
                    << session->client_label
                    << ", reason=" << detail::SocketErrorToString(socket_error) << "\n";
        }
        break;
      }

#ifdef _WIN32
      const int sent = sendto(listen_socket_, reinterpret_cast<const char*>(buffer.data()),
                              static_cast<int>(received), 0,
                              reinterpret_cast<const sockaddr*>(&session->client_addr),
                              session->client_len);
#else
      const ssize_t sent = sendto(listen_socket_, buffer.data(),
                                  static_cast<std::size_t>(received), 0,
                                  reinterpret_cast<const sockaddr*>(&session->client_addr),
                                  session->client_len);
#endif
      if (sent <= 0) {
        const int socket_error = detail::LastSocketError();
        std::cout << "[udp] target->client send failed: client="
                  << session->client_label << ", bytes=" << received
                  << ", reason=" << detail::SocketErrorToString(socket_error) << "\n";
        break;
      }
      session->target_to_client_packets.fetch_add(1, std::memory_order_relaxed);
      session->target_to_client_bytes.fetch_add(static_cast<std::uint64_t>(sent),
                                                std::memory_order_relaxed);
      if (config_.udp_debug) {
        std::cout << "[udp] target->client: client=" << session->client_label
                  << ", bytes=" << sent
                  << ", from=" << EndpointToString(remote_addr, remote_len) << "\n";
      }
      session->last_active_ms.store(NowMonotonicMs(), std::memory_order_relaxed);
    }

    session->marked_for_close.store(true);
  }

  void MaybeCleanupIdleUdpSessions() {
    const std::uint64_t now = NowMonotonicMs();
    if (now - last_udp_cleanup_ms_ < kUdpCleanupIntervalMs) {
      return;
    }
    last_udp_cleanup_ms_ = now;
    CleanupIdleUdpSessions(false);
  }

  void CleanupIdleUdpSessions(bool force_all) {
    struct ClosingUdpSession {
      std::shared_ptr<UdpSession> session;
      std::string reason;
    };
    std::vector<ClosingUdpSession> closing_sessions;
    const std::uint64_t now = NowMonotonicMs();
    const std::uint64_t idle_timeout_ms =
        static_cast<std::uint64_t>(config_.udp_session_idle_timeout_seconds) * 1000;
    {
      std::lock_guard<std::mutex> lock(udp_session_mutex_);
      for (auto it = udp_sessions_.begin(); it != udp_sessions_.end();) {
        const auto& session = it->second;
        const bool idle_timeout =
            idle_timeout_ms > 0 &&
            now > session->last_active_ms.load(std::memory_order_relaxed) &&
            now - session->last_active_ms.load(std::memory_order_relaxed) > idle_timeout_ms;
        const bool should_close =
            force_all || session->marked_for_close.load() || idle_timeout;
        if (!should_close) {
          ++it;
          continue;
        }
        std::string reason = "marked_for_close";
        if (force_all) {
          reason = "stopping";
        } else if (idle_timeout) {
          reason = "idle_timeout";
        }
        session->marked_for_close.store(true);
        closing_sessions.push_back({session, reason});
        it = udp_sessions_.erase(it);
      }
    }

    for (const auto& entry : closing_sessions) {
      const auto& session = entry.session;
      const std::uint64_t idle_ms =
          now > session->last_active_ms.load(std::memory_order_relaxed)
              ? now - session->last_active_ms.load(std::memory_order_relaxed)
              : 0;
      std::cout << "[udp] client disconnected: " << session->client_label
                << ", reason=" << entry.reason << ", idle_ms=" << idle_ms;
      if (config_.udp_debug) {
        std::cout << ", client_to_target_packets="
                  << session->client_to_target_packets.load(std::memory_order_relaxed)
                  << ", client_to_target_bytes="
                  << session->client_to_target_bytes.load(std::memory_order_relaxed)
                  << ", target_to_client_packets="
                  << session->target_to_client_packets.load(std::memory_order_relaxed)
                  << ", target_to_client_bytes="
                  << session->target_to_client_bytes.load(std::memory_order_relaxed);
      }
      std::cout << "\n";
      detail::ShutdownBoth(session->upstream_socket);
      detail::CloseSocket(session->upstream_socket);
      session->upstream_socket = kInvalidSocket;
      if (session->upstream_to_client_thread.joinable()) {
        session->upstream_to_client_thread.join();
      }
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
      std::size_t active_sessions = 0;
      const std::string peer_label = EndpointToString(peer, peer_len);
      {
        std::lock_guard<std::mutex> lock(session_mutex_);
        session_id = ++next_session_id_;
        session = std::make_shared<detail::ForwardSession>(client, upstream);
        sessions_[session_id] = session;
        session_peers_[session_id] = peer_label;
        active_sessions = sessions_.size();
      }
      std::cout << "[tcp] client connected: " << peer_label
                << ", active_sessions=" << active_sessions << "\n";
      session->Start();
    }

    CleanupFinishedSessions();
  }

  void CleanupFinishedSessions() {
    std::vector<std::shared_ptr<detail::ForwardSession>> finished_sessions;
    std::vector<std::string> finished_peers;
    {
      std::lock_guard<std::mutex> lock(session_mutex_);
      for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->second != nullptr && it->second->IsFinished()) {
          finished_sessions.push_back(it->second);
          const auto peer_it = session_peers_.find(it->first);
          if (peer_it != session_peers_.end()) {
            finished_peers.push_back(peer_it->second);
            session_peers_.erase(peer_it);
          } else {
            finished_peers.emplace_back("unknown");
          }
          it = sessions_.erase(it);
        } else {
          ++it;
        }
      }
    }
    for (const auto& session : finished_sessions) {
      session->Join();
    }
    for (const auto& peer : finished_peers) {
      std::cout << "[tcp] client disconnected: " << peer << "\n";
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
  std::unordered_map<std::uint64_t, std::string> session_peers_;
  std::uint64_t next_session_id_{0};

  static constexpr std::uint64_t kUdpCleanupIntervalMs = 1000;
  mutable std::mutex udp_session_mutex_;
  std::unordered_map<std::string, std::shared_ptr<UdpSession>> udp_sessions_;
  std::uint64_t last_udp_cleanup_ms_{0};
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
