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
  // Legacy/default family preference. If listen_family/target_family stay auto
  // and this is explicit, both sides use this value for backwards compatibility.
  AddressFamilyPreference family = AddressFamilyPreference::kAuto;
  AddressFamilyPreference listen_family = AddressFamilyPreference::kAuto;
  AddressFamilyPreference target_family = AddressFamilyPreference::kAuto;
  std::uint32_t dns_refresh_interval_minutes = 10;
  std::uint32_t udp_session_idle_timeout_seconds = 300;
  bool udp_debug = false;
  int backlog = 128;
};

}  // namespace portforwardx::net
