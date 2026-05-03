#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "portforwardx/net/forward_config.hpp"

namespace portforwardx::net {

class Forwarder {
 public:
  explicit Forwarder(ForwardConfig config);
  ~Forwarder();

  Forwarder(const Forwarder&) = delete;
  Forwarder& operator=(const Forwarder&) = delete;

  bool Start(std::string* error_message);
  void Stop();

  bool IsRunning() const;
  std::uint16_t ListeningPort() const;
  std::string ListeningHost() const;

 private:
  class Impl;

  ForwardConfig config_;
  std::atomic<bool> running_{false};
  std::uint16_t listening_port_{0};
  std::string listening_host_;
  mutable std::mutex state_mutex_;
  Impl* impl_{nullptr};
};

AddressFamilyPreference ParseAddressFamily(const std::string& input);
std::string AddressFamilyToString(AddressFamilyPreference family);
TransportProtocol ParseTransportProtocol(const std::string& input);
std::string TransportProtocolToString(TransportProtocol protocol);

}  // namespace portforwardx::net
