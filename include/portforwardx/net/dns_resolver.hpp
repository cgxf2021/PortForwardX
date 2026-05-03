#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "portforwardx/net/forward_config.hpp"

namespace portforwardx::net {

struct ResolvedEndpoint {
  std::string ip;
  std::uint16_t port = 0;
  int family = 0;
  std::vector<std::uint8_t> sockaddr_bytes;
  std::size_t sockaddr_len = 0;
};

class DnsResolver {
 public:
  static std::vector<ResolvedEndpoint> Resolve(
      const std::string& host,
      std::uint16_t port,
      TransportProtocol protocol,
      AddressFamilyPreference family,
      bool passive,
      std::string* error_message);
};

}  // namespace portforwardx::net
