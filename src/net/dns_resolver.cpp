#include "portforwardx/net/dns_resolver.hpp"

#include <algorithm>
#include <cstring>

#include "net/socket_platform.hpp"

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

std::string ToIpString(const sockaddr* addr) {
  char buffer[INET6_ADDRSTRLEN] = {};
  if (addr->sa_family == AF_INET) {
    const auto* addr4 = reinterpret_cast<const sockaddr_in*>(addr);
    if (inet_ntop(AF_INET, &(addr4->sin_addr), buffer, sizeof(buffer)) != nullptr) {
      return buffer;
    }
  } else if (addr->sa_family == AF_INET6) {
    const auto* addr6 = reinterpret_cast<const sockaddr_in6*>(addr);
    if (inet_ntop(AF_INET6, &(addr6->sin6_addr), buffer, sizeof(buffer)) != nullptr) {
      return buffer;
    }
  }
  return {};
}

std::uint16_t ExtractPort(const sockaddr* addr) {
  if (addr->sa_family == AF_INET) {
    const auto* addr4 = reinterpret_cast<const sockaddr_in*>(addr);
    return ntohs(addr4->sin_port);
  }
  if (addr->sa_family == AF_INET6) {
    const auto* addr6 = reinterpret_cast<const sockaddr_in6*>(addr);
    return ntohs(addr6->sin6_port);
  }
  return 0;
}

}  // namespace

std::vector<ResolvedEndpoint> DnsResolver::Resolve(
    const std::string& host,
    std::uint16_t port,
    TransportProtocol protocol,
    AddressFamilyPreference family,
    bool passive,
    std::string* error_message) {
  if (!detail::EnsureSocketApiInitialized(error_message)) {
    return {};
  }

  addrinfo hints{};
  hints.ai_family = ToNativeFamily(family);
  if (protocol == TransportProtocol::kUdp) {
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
  } else {
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
  }
  hints.ai_flags = 0;
  if (passive) {
    hints.ai_flags |= AI_PASSIVE;
  }

  const std::string port_text = std::to_string(port);
  const char* node = host.empty() ? nullptr : host.c_str();

  addrinfo* result = nullptr;
  const int rc = getaddrinfo(node, port_text.c_str(), &hints, &result);
  if (rc != 0) {
    if (error_message != nullptr) {
#ifdef _WIN32
      *error_message = "getaddrinfo failed: " + std::to_string(rc);
#else
      *error_message = "getaddrinfo failed: " + std::string(gai_strerror(rc));
#endif
    }
    return {};
  }

  std::vector<ResolvedEndpoint> endpoints;
  for (addrinfo* it = result; it != nullptr; it = it->ai_next) {
    if (it->ai_addr == nullptr || it->ai_addrlen <= 0) {
      continue;
    }
    ResolvedEndpoint endpoint;
    endpoint.family = it->ai_family;
    endpoint.port = ExtractPort(it->ai_addr);
    endpoint.ip = ToIpString(it->ai_addr);
    endpoint.sockaddr_len = static_cast<std::size_t>(it->ai_addrlen);
    endpoint.sockaddr_bytes.resize(endpoint.sockaddr_len);
    std::memcpy(endpoint.sockaddr_bytes.data(), it->ai_addr, endpoint.sockaddr_len);
    endpoints.push_back(std::move(endpoint));
  }

  freeaddrinfo(result);

  if (endpoints.empty() && error_message != nullptr) {
    *error_message = "No endpoint resolved for host: " + host;
  }
  return endpoints;
}

}  // namespace portforwardx::net
