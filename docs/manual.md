# PortForwardX Manual

## 1. Overview

PortForwardX is a TCP/UDP port forward tool supporting:

- IPv4 -> IPv4
- IPv6 -> IPv6
- IPv4 <-> IPv6
- Domain name target resolution (A / AAAA)

## 2. Build Prerequisites

- CMake >= 3.16
- C++17 compiler (gcc/g++ on Linux, MSVC on Windows)
- Prebuilt Boost.Program_options in project-local third-party directory

## 3. Third-Party Build (Project Local)

The Boost dependency is built and installed into:

- `third_party/install/boost`

Linux static example:

```bash
./third_party/build_boost.sh static
```

Windows static example:

```powershell
pwsh -ExecutionPolicy Bypass -File .\third_party\build_boost.ps1 -LinkType static
```

## 4. Configure and Build

Linux:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPORTFORWARDX_THIRD_PARTY_LINK=STATIC
cmake --build build
```

Windows:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DPORTFORWARDX_THIRD_PARTY_LINK=STATIC
cmake --build build --config Release
```

## 5. Install to a Deploy Directory

Install into a local directory (example: `dist/install`):

Linux:

```bash
cmake --install build --prefix dist/install
```

Windows:

```bat
cmake --install build --config Release --prefix dist/install
```

The install result contains:

- `bin/portforwardx` (or `bin/portforwardx.exe`)
- runtime third-party shared dependencies copied to `bin/` (when needed)
- `manual.md` (this file)

## 6. Runtime Arguments

Common arguments:

- `--mode` `custom | server | client` (default `custom`)
- `--protocol` `tcp | udp | both` (default `tcp`)
- `--family` `auto | ipv4 | ipv6` (default `auto`)
  - In `custom` mode, the value applies to both listen and target sides.
  - In `server` mode, `auto` listens on IPv6 and resolves the target automatically.
  - In `client` mode, `auto` listens locally on IPv4 and resolves the remote target as IPv6.
- `--dns-refresh-min` periodic target DNS refresh interval in minutes (default `10`)
- `--udp-idle-timeout-sec` UDP session idle timeout in seconds (default `300`, `0` disables timeout)
- `--udp-debug` print UDP packet forwarding diagnostics (default off)

`custom` mode (legacy/general):

- `--listen-host` listen host/address (default `::`)
- `--listen-port` listen port (required)
- `--target-host` target host/address/domain (required)
- `--target-port` target port (required)

`server` mode (public IPv6 ingress -> private IPv4 game server):

- `--public-listen-host` public listen host/address (default `::`)
- `--public-listen-port` public listen port (required)
- `--lan-target-host` private/LAN target host/address (default `127.0.0.1`)
- `--lan-target-port` private/LAN target port (required)

`client` mode (local IPv4 app -> remote public IPv6 server):

- `--local-listen-host` local listen host/address (default `127.0.0.1`)
- `--local-listen-port` local listen port (required)
- `--server-ipv6-host` remote IPv6/hostname (required)
- `--server-port` remote server port (required)

Examples:

```bash
# server side
./portforwardx --mode server --public-listen-host :: --public-listen-port 25565 --lan-target-host 127.0.0.1 --lan-target-port 25565 --protocol both --family auto

# client side
./portforwardx --mode client --local-listen-host 127.0.0.1 --local-listen-port 25565 --server-ipv6-host 2001:db8::10 --server-port 25565 --protocol both --family auto

# custom mode
./portforwardx --mode custom --listen-host 127.0.0.1 --listen-port 18080 --target-host example.com --target-port 443 --protocol tcp --family auto --dns-refresh-min 10
```

Do not use wildcard addresses such as `0.0.0.0` as a target host. They are valid for listening/binding only. Use a concrete target address such as `127.0.0.1` or the LAN IPv4 address of the game server.

Script debug examples:

```bash
UDP_DEBUG=1 ./run_server.sh
```

```powershell
.\run_server.ps1 -UdpDebug
```

Runtime logs include:

- `[tcp] client connected/disconnected ...`
- `[udp] client connected/disconnected ...` with cleanup reason
- `[dns] refresh ok/failed ...` for each periodic DNS refresh. Successful refreshes include all resolved IPv4/IPv6 endpoints, for example:

```text
[dns] refresh ok: example.com:443 -> ipv4 93.184.216.34:443, ipv6 [2606:2800:220:1:248:1893:25c8:1946]:443
```

When `--udp-debug` is enabled, UDP packet-level diagnostics are also printed:

- `[udp] client->target ...` confirms a packet was forwarded to the target.
- `[udp] target->client ...` confirms a reply was received from the target and forwarded back to the original client.

## 7. End-to-end tests

Only E2E tests are shipped under `tests/`:

- `local_e2e_test.cpp` (TCP): concurrent clients, multi-round echo validation
- `local_udp_e2e_test.cpp` (UDP): local echo server + forwarder round-trip

Run all tests:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPORTFORWARDX_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## 8. Notes

- If `PORTFORWARDX_THIRD_PARTY_LINK=STATIC`, runtime dependency copying may copy nothing, which is expected.
- If `PORTFORWARDX_THIRD_PARTY_LINK=SHARED`, ensure the third-party shared libraries were built first.
