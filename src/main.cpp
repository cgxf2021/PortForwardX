#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

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
      "listen-host", po::value<std::string>()->default_value("::"),
      "Listen host/IP (v4/v6/domain)")(
      "listen-port", po::value<unsigned int>(), "Listen port [0..65535]")(
      "target-host", po::value<std::string>(), "Target host/IP/domain")(
      "target-port", po::value<unsigned int>(), "Target port [0..65535]")(
      "protocol", po::value<std::string>()->default_value("tcp"),
      "Transport protocol: tcp|udp")(
      "family", po::value<std::string>()->default_value("auto"),
      "Address family strategy: auto|ipv4|ipv6")(
      "dns-refresh-sec", po::value<unsigned int>()->default_value(30),
      "Target DNS refresh interval seconds");

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

  if (vm.count("listen-port") == 0 || vm.count("target-host") == 0 ||
      vm.count("target-port") == 0) {
    std::cerr << "Missing required arguments.\n\n";
    std::cerr << options << "\n";
    return 1;
  }

  const auto listen_port_raw = vm["listen-port"].as<unsigned int>();
  const auto target_port_raw = vm["target-port"].as<unsigned int>();
  if (listen_port_raw > 65535U || target_port_raw > 65535U) {
    std::cerr << "Port must be within [0, 65535].\n";
    return 1;
  }

  config.listen_host = vm["listen-host"].as<std::string>();
  config.listen_port = static_cast<std::uint16_t>(listen_port_raw);
  config.target_host = vm["target-host"].as<std::string>();
  config.target_port = static_cast<std::uint16_t>(target_port_raw);
  config.protocol =
      portforwardx::net::ParseTransportProtocol(vm["protocol"].as<std::string>());
  config.family =
      portforwardx::net::ParseAddressFamily(vm["family"].as<std::string>());
  config.dns_refresh_interval_seconds = vm["dns-refresh-sec"].as<unsigned int>();

  std::signal(SIGINT, HandleSignal);
#ifdef SIGTERM
  std::signal(SIGTERM, HandleSignal);
#endif

  portforwardx::net::Forwarder forwarder(config);
  std::string error_message;
  if (!forwarder.Start(&error_message)) {
    std::cerr << "Failed to start forwarder: " << error_message << "\n";
    return 2;
  }

  std::cout << "Forwarding started.\n"
            << "  listen: " << forwarder.ListeningHost() << ":" << forwarder.ListeningPort()
            << "\n"
            << "  target: " << config.target_host << ":" << config.target_port << "\n"
            << "  protocol: " << portforwardx::net::TransportProtocolToString(config.protocol)
            << "\n"
            << "  family: " << portforwardx::net::AddressFamilyToString(config.family) << "\n"
            << "  dns refresh: " << config.dns_refresh_interval_seconds << "s\n"
            << "Press Ctrl+C to stop.\n";

  while (!g_stop_requested.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  forwarder.Stop();
  std::cout << "Forwarding stopped.\n";
  return 0;
}
