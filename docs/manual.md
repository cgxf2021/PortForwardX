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

- `--listen-host` listen host/address (default `::`)
- `--listen-port` listen port (required)
- `--target-host` target host/address/domain (required)
- `--target-port` target port (required)
- `--protocol` `tcp | udp` (default `tcp`)
- `--family` `auto | ipv4 | ipv6` (default `auto`)
- `--dns-refresh-sec` periodic target DNS refresh interval in seconds (default `30`)

Example:

```bash
./portforwardx --listen-host 127.0.0.1 --listen-port 18080 --target-host example.com --target-port 443 --protocol tcp --family auto --dns-refresh-sec 10
```

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
