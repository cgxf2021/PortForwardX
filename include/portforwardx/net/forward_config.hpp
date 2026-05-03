#pragma once

#include <cstdint>
#include <string>

namespace portforwardx::net {

enum class TransportProtocol {
  kTcp = 0,
  kUdp,
};

enum class AddressFamilyPreference {
  kAuto = 0,
  kIPv4Only,
  kIPv6Only,
};

struct ForwardConfig {
  std::string listen_host = "::";
  std::uint16_t listen_port = 0;
  std::string target_host;
  std::uint16_t target_port = 0;
  TransportProtocol protocol = TransportProtocol::kTcp;
  AddressFamilyPreference family = AddressFamilyPreference::kAuto;
  std::uint32_t dns_refresh_interval_seconds = 30;
  int backlog = 128;
};

}  // namespace portforwardx::net
