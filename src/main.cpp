#include <atomic>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <boost/program_options.hpp>

#include "portforwardx/portforwardx.hpp"

namespace {

std::atomic<bool> g_stop_requested{false};

void HandleSignal(int) { g_stop_requested.store(true); }

}  // namespace

int main(int argc, char** argv) {
  namespace po = boost::program_options;

  portforwardx::net::ForwardConfig config;
  config.listen_host = "::";
  config.family = portforwardx::net::AddressFamilyPreference::kAuto;

  po::options_description options("PortForwardX options");
  options.add_options()("help,h", "Print help message")(
      "mode", po::value<std::string>()->default_value("custom"),
      "Run mode: custom|server|client")(
      "protocol", po::value<std::string>()->default_value("tcp"),
      "Transport protocol: tcp|udp|both")(
      "family", po::value<std::string>()->default_value("auto"),
      "Address family strategy: custom=both sides, server=public listen, client=remote target")(
      "dns-refresh-min", po::value<unsigned int>()->default_value(10),
      "Target DNS refresh interval minutes")(
      "udp-idle-timeout-sec", po::value<unsigned int>()->default_value(300),
      "UDP session idle timeout seconds (0 to disable)")(
      "udp-debug", po::bool_switch()->default_value(false),
      "Print UDP packet forwarding diagnostics")

      // custom mode (legacy/general purpose)
      ("listen-host", po::value<std::string>()->default_value("::"),
       "Custom mode: listen host/IP (v4/v6/domain)")(
          "listen-port", po::value<unsigned int>(),
          "Custom mode: listen port [0..65535]")(
          "target-host", po::value<std::string>(),
          "Custom mode: target host/IP/domain")(
          "target-port", po::value<unsigned int>(),
          "Custom mode: target port [0..65535]")

      // server mode: public IPv6 -> private IPv4 service
      ("public-listen-host", po::value<std::string>()->default_value("::"),
       "Server mode: public listen host/IP (normally IPv6)")(
          "public-listen-port", po::value<unsigned int>(),
          "Server mode: public listen port [0..65535]")(
          "lan-target-host", po::value<std::string>()->default_value("127.0.0.1"),
          "Server mode: private/LAN target host (usually IPv4)")(
          "lan-target-port", po::value<unsigned int>(),
          "Server mode: private/LAN target port [0..65535]")

      // client mode: local IPv4 app -> remote public IPv6
      ("local-listen-host", po::value<std::string>()->default_value("127.0.0.1"),
       "Client mode: local listen host/IP (usually IPv4)")(
          "local-listen-port", po::value<unsigned int>(),
          "Client mode: local listen port [0..65535]")(
          "server-ipv6-host", po::value<std::string>(),
          "Client mode: remote public IPv6 or hostname")(
          "server-port", po::value<unsigned int>(),
          "Client mode: remote server port [0..65535]");

  po::variables_map vm;
  try {
    po::store(po::parse_command_line(argc, argv, options), vm);
    po::notify(vm);
  } catch (const std::exception& ex) {
    std::cerr << "Argument parse error: " << ex.what() << "\n\n";
    std::cerr << options << "\n";
    return 1;
  }

  if (vm.count("help") > 0) {
    std::cout << "PortForwardX " << portforwardx::version_string() << "\n\n";
    std::cout << options << "\n";
    return 0;
  }

  const auto run_mode = vm["mode"].as<std::string>();
  const auto family_preference =
      portforwardx::net::ParseAddressFamily(vm["family"].as<std::string>());
  config.family = family_preference;
  auto parse_port = [&](const char* key, unsigned int* value_out) -> bool {
    if (vm.count(key) == 0) {
      std::cerr << "Missing required argument: --" << key << "\n\n";
      std::cerr << options << "\n";
      return false;
    }
    const auto raw = vm[key].as<unsigned int>();
    if (raw > 65535U) {
      std::cerr << "Invalid value for --" << key << ": " << raw
                << " (must be within [0, 65535]).\n";
      return false;
    }
    *value_out = raw;
    return true;
  };

  unsigned int listen_port_raw = 0;
  unsigned int target_port_raw = 0;

  if (run_mode == "custom") {
    if (!parse_port("listen-port", &listen_port_raw) ||
        !parse_port("target-port", &target_port_raw)) {
      return 1;
    }
    if (vm.count("target-host") == 0) {
      std::cerr << "Missing required argument: --target-host\n\n";
      std::cerr << options << "\n";
      return 1;
    }
    config.listen_host = vm["listen-host"].as<std::string>();
    config.listen_port = static_cast<std::uint16_t>(listen_port_raw);
    config.target_host = vm["target-host"].as<std::string>();
    config.target_port = static_cast<std::uint16_t>(target_port_raw);
    config.listen_family = family_preference;
    config.target_family = family_preference;
  } else if (run_mode == "server") {
    if (!parse_port("public-listen-port", &listen_port_raw) ||
        !parse_port("lan-target-port", &target_port_raw)) {
      return 1;
    }
    config.listen_host = vm["public-listen-host"].as<std::string>();
    config.listen_port = static_cast<std::uint16_t>(listen_port_raw);
    config.target_host = vm["lan-target-host"].as<std::string>();
    config.target_port = static_cast<std::uint16_t>(target_port_raw);
    config.listen_family =
        family_preference == portforwardx::net::AddressFamilyPreference::kAuto
            ? portforwardx::net::AddressFamilyPreference::kIPv6Only
            : family_preference;
    config.target_family = portforwardx::net::AddressFamilyPreference::kAuto;
  } else if (run_mode == "client") {
    if (!parse_port("local-listen-port", &listen_port_raw) ||
        !parse_port("server-port", &target_port_raw)) {
      return 1;
    }
    if (vm.count("server-ipv6-host") == 0) {
      std::cerr << "Missing required argument: --server-ipv6-host\n\n";
      std::cerr << options << "\n";
      return 1;
    }
    config.listen_host = vm["local-listen-host"].as<std::string>();
    config.listen_port = static_cast<std::uint16_t>(listen_port_raw);
    config.target_host = vm["server-ipv6-host"].as<std::string>();
    config.target_port = static_cast<std::uint16_t>(target_port_raw);
    config.listen_family = portforwardx::net::AddressFamilyPreference::kIPv4Only;
    config.target_family =
        family_preference == portforwardx::net::AddressFamilyPreference::kAuto
            ? portforwardx::net::AddressFamilyPreference::kIPv6Only
            : family_preference;
  } else {
    std::cerr << "Invalid run mode: " << run_mode
              << " (expected one of: custom|server|client)\n\n";
    std::cerr << options << "\n";
    return 1;
  }

  auto protocol_input = vm["protocol"].as<std::string>();
  std::transform(protocol_input.begin(), protocol_input.end(), protocol_input.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const bool enable_tcp = (protocol_input == "tcp" || protocol_input == "both");
  const bool enable_udp = (protocol_input == "udp" || protocol_input == "both");
  if (!enable_tcp && !enable_udp) {
    std::cerr << "Invalid value for --protocol: " << protocol_input
              << " (expected one of: tcp|udp|both)\n";
    return 1;
  }
  config.dns_refresh_interval_minutes = vm["dns-refresh-min"].as<unsigned int>();
  config.udp_session_idle_timeout_seconds =
      vm["udp-idle-timeout-sec"].as<unsigned int>();
  config.udp_debug = vm["udp-debug"].as<bool>();

  std::signal(SIGINT, HandleSignal);
#ifdef SIGTERM
  std::signal(SIGTERM, HandleSignal);
#endif

  struct RunningForwarder {
    portforwardx::net::TransportProtocol protocol;
    std::unique_ptr<portforwardx::net::Forwarder> instance;
  };
  std::vector<RunningForwarder> forwarders;
  forwarders.reserve(2);

  auto start_forwarder = [&](portforwardx::net::TransportProtocol protocol) -> bool {
    auto per_protocol_config = config;
    per_protocol_config.protocol = protocol;
    auto forwarder = std::make_unique<portforwardx::net::Forwarder>(per_protocol_config);
    std::string error_message;
    if (!forwarder->Start(&error_message)) {
      std::cerr << "Failed to start "
                << portforwardx::net::TransportProtocolToString(protocol)
                << " forwarder: " << error_message << "\n";
      return false;
    }
    forwarders.push_back({protocol, std::move(forwarder)});
    return true;
  };

  if (enable_tcp &&
      !start_forwarder(portforwardx::net::TransportProtocol::kTcp)) {
    return 2;
  }
  if (enable_udp &&
      !start_forwarder(portforwardx::net::TransportProtocol::kUdp)) {
    for (auto& running : forwarders) {
      running.instance->Stop();
    }
    return 2;
  }

  std::cout << "Forwarding started.\n"
            << "  mode: " << run_mode << "\n"
            << "  target: " << config.target_host << ":" << config.target_port << "\n"
            << "  protocol: " << protocol_input << "\n"
            << "  family: " << portforwardx::net::AddressFamilyToString(config.family)
            << " (listen="
            << portforwardx::net::AddressFamilyToString(config.listen_family)
            << ", target="
            << portforwardx::net::AddressFamilyToString(config.target_family) << ")\n"
            << "  dns refresh: " << config.dns_refresh_interval_minutes << "min\n"
            << "  udp idle timeout: " << config.udp_session_idle_timeout_seconds
            << "s (0=disabled)\n"
            << "  udp debug: " << (config.udp_debug ? "on" : "off") << "\n";
  for (const auto& running : forwarders) {
    std::cout << "  listen(" << portforwardx::net::TransportProtocolToString(running.protocol)
              << "): " << running.instance->ListeningHost() << ":"
              << running.instance->ListeningPort() << "\n";
  }
  std::cout << "Press Ctrl+C to stop.\n";

  while (!g_stop_requested.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  for (auto& running : forwarders) {
    running.instance->Stop();
  }
  std::cout << "Forwarding stopped.\n";
  return 0;
}
