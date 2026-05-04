# PortForwardX

C++17 项目，使用 CMake 构建；支持在 **Linux（GCC/G++）** 与 **Windows 11（MSVC）** 上编译。

## 依赖

- CMake 3.16 或更高
- C++17 编译器：Linux 上为 GCC；Windows 上为 Visual Studio 2022 自带的 MSVC（推荐 x64）
- `curl` + `tar`（Linux）或 PowerShell（Windows），用于下载/解压 Boost 源码

可选：

- [Ninja](https://ninja-build.org/)：用于下文 **CMake 预设** 或 Linux 下的 Ninja 生成器

---

## 第三方库管理（本地预编译）

项目采用 **Boost 生态优先**，当前接入：

- `Boost.Program_options`（CLI 参数解析）

第三方库源码与产物都放在项目内，不安装到系统目录：

- 源码：`third_party/src`
- 中间编译：`third_party/build`
- 安装产物：`third_party/install`

### 1) 预编译第三方库（一次即可，后续复用）

Linux（静态库）：

```bash
./third_party/build_boost.sh static
```

Windows PowerShell（静态库）：

```powershell
pwsh -ExecutionPolicy Bypass -File .\third_party\build_boost.ps1 -LinkType static
```

如果要改为动态库，把 `static` 改成 `shared` 即可。

### 2) CMake 通过项目内安装目录查找并链接

`CMakeLists.txt` 已配置：

- `Boost_ROOT` 指向 `third_party/install/boost`，并加入 `CMAKE_PREFIX_PATH`
- `find_package(Boost 1.83 CONFIG COMPONENTS program_options)`

并提供链接模式开关：

- `PORTFORWARDX_THIRD_PARTY_LINK=STATIC`（默认）
- `PORTFORWARDX_THIRD_PARTY_LINK=SHARED`

---

## Linux（GCC / G++）

### 方式一：命令行（Ninja + Release）

在项目根目录执行：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DPORTFORWARDX_THIRD_PARTY_LINK=STATIC
cmake --build build
```

可执行文件：`build/portforwardx`（示例程序，链接静态库 `portforwardx_core`）。

查看帮助与参数说明：

```bash
./build/portforwardx --help
```

Debug 构建将 `Release` 改为 `Debug` 即可。

### 方式二：CMake 预设（需在 Linux 主机上，且已安装 Ninja）

```bash
cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release
```

产物目录：`build/linux-gcc-release/`，可执行文件：`build/linux-gcc-release/portforwardx`。

---

## Windows 11（MSVC）

### 方式一：Visual Studio 打开文件夹

1. 安装 **Visual Studio 2022**，勾选「使用 C++ 的桌面开发」与工作负载中的 MSVC、Windows SDK。
2. 用 VS 打开本仓库根目录（**文件 → 打开 → CMake…**）。
3. 在 CMake 目标列表中选择 `portforwardx`，选择 **Release** 或 **Debug** 配置后生成并运行。

### 方式二：CMake 预设（开发者 PowerShell / cmd，已配置 PATH 中的 `cmake`）

使用与仓库中 `CMakePresets.json` 一致的 **Visual Studio 17 2022** 生成器（x64）：

```bat
cmake --preset win-msvc-release -DPORTFORWARDX_THIRD_PARTY_LINK=STATIC
cmake --build --preset win-msvc-release --config Release
```

可执行文件位于多配置生成器的 **配置子目录** 下，例如：

`build\win-msvc-release\Release\portforwardx.exe`

Debug 时把预设与 `--config` 改为 `win-msvc-debug` 与 `Debug`。

### 方式三：手动指定生成器（不依赖预设）

在仓库根目录：

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DPORTFORWARDX_THIRD_PARTY_LINK=STATIC
cmake --build build --config Release
```

可执行文件示例路径：`build\Release\portforwardx.exe`（具体以生成器输出为准）。

---

## CMake 选项（两平台通用）

| 选项 | 默认 | 说明 |
|------|------|------|
| `PORTFORWARDX_BUILD_EXAMPLES` | `ON` | 是否构建示例可执行文件 `portforwardx` |
| `PORTFORWARDX_BUILD_TESTS` | `ON` | 是否构建端到端测试（`tests/local_*_e2e_test.cpp`） |
| `PORTFORWARDX_ENABLE_WARNINGS_AS_ERRORS` | `OFF` | 是否将警告视为错误 |
| `PORTFORWARDX_THIRD_PARTY_LINK` | `STATIC` | 第三方库链接方式：`STATIC` 或 `SHARED` |
| `PORTFORWARDX_USE_ONLY_LOCAL_THIRD_PARTY` | `ON` | 仅在项目内 `third_party/install` 搜索第三方库 |
| `PORTFORWARDX_INSTALL_RUNTIME_DEPS` | `ON` | 安装时复制可执行程序所需的项目内第三方动态库到 `bin/` |

示例：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPORTFORWARDX_ENABLE_WARNINGS_AS_ERRORS=ON
```

在 Windows 多配置生成器上，在首次 `cmake -S ...` 之后用 `cmake --build build --config Release` 时可通过 `cmake -LAH` 或在 CMake GUI 中修改缓存变量。

---

## 端口转发使用示例

`PortForwardX` 支持三种运行模式：

- `--mode server`：公网入口（通常 IPv6）转发到内网游戏服务（通常 IPv4）
- `--mode client`：本地 IPv4 入口转发到远端公网 IPv6 服务
- `--mode custom`：通用模式（兼容原始 `listen/target` 参数）

通用参数（所有模式可用）：

- `--protocol`：`tcp` / `udp` / `both`（默认 `tcp`）
- `--family`：`auto` / `ipv4` / `ipv6`（默认 `auto`）
  - `custom` 模式：同时作用于监听侧和目标侧
  - `server` 模式：`auto` 时公网监听侧默认使用 IPv6，目标侧自动解析
  - `client` 模式：`auto` 时本地监听侧默认使用 IPv4，远端目标侧默认使用 IPv6
- `--dns-refresh-min`：目标域名刷新间隔分钟数（默认 `10`）
- `--udp-idle-timeout-sec`：UDP 会话空闲超时秒数（默认 `300`，`0` 表示不超时）
- `--udp-debug`：打印 UDP 包级转发诊断日志（默认关闭）
- 每次 DNS 刷新会打印详细映射：`[dns] refresh ok: example.com:443 -> ipv4 1.2.3.4:443, ipv6 [2001:db8::1]:443`

### 方案一：服务端（公网 IPv6 -> 本地 IPv4 游戏服）

假设：

- 服务器公网 IPv6：`2001:db8::10`
- 游戏服只监听本机 IPv4：`127.0.0.1:25565`
- 对外暴露端口：`25565`

Linux:

```bash
./build/portforwardx --mode server --public-listen-host :: --public-listen-port 25565 --lan-target-host 127.0.0.1 --lan-target-port 25565 --protocol both --family auto
```

Windows:

```bat
build\win-msvc-release\Release\portforwardx.exe --mode server --public-listen-host :: --public-listen-port 25565 --lan-target-host 127.0.0.1 --lan-target-port 25565 --protocol both --family auto
```

注意：`--lan-target-host` 必须是可连接的具体地址，例如 `127.0.0.1` 或服务器内网 IPv4。`0.0.0.0` 只适合作为服务程序监听地址，不能作为 PortForwardX 的转发目标。

### 方案二：客户端（本地 IPv4 -> 远端公网 IPv6）

客户端机器本地游戏程序只会连 IPv4，可先连本地转发入口：

```bash
./build/portforwardx --mode client --local-listen-host 127.0.0.1 --local-listen-port 25565 --server-ipv6-host 2001:db8::10 --server-port 25565 --protocol both --family auto
```

然后在游戏客户端内填：

- 服务器地址：`127.0.0.1`
- 端口：`25565`

### 服务端运行日志

运行中会输出以下关键日志，便于排查连通性：

- `[tcp] client connected/disconnected ...`：TCP 客户端连接与断连
- `[udp] client connected/disconnected ...`：UDP 客户端会话创建与清理（含原因）
- `[dns] refresh ok/failed ...`：目标域名周期刷新结果；成功时会列出 IPv4/IPv6 解析地址
- 开启 `--udp-debug` 后会额外输出 `[udp] client->target ...` 与 `[udp] target->client ...`，用于确认 UDP 包是否进入目标服务以及是否有回包

脚本开启 UDP debug：

```bat
set UDP_DEBUG=1
run_server.bat
```

PowerShell:

```powershell
.\run_server.ps1 -UdpDebug
```

### 自定义模式（兼容旧参数）

`--mode custom` 使用原始参数：

- `--listen-host` 监听地址（默认 `::`）
- `--listen-port` 监听端口（必填）
- `--target-host` 目标地址（必填）
- `--target-port` 目标端口（必填）

示例：

```bash
./build/portforwardx --mode custom --listen-host 127.0.0.1 --listen-port 18080 --target-host 1.1.1.1 --target-port 80 --protocol tcp --family ipv4
```

### 域名解析示例

域名可以直接作为目标地址，程序会通过 DNS 解析 A/AAAA 记录：

```bash
./build/portforwardx --mode custom --listen-host :: --listen-port 19000 --target-host example.com --target-port 443 --protocol tcp --family auto --dns-refresh-min 10
```

刷新日志示例：

```text
[dns] refresh ok: example.com:443 -> ipv4 93.184.216.34:443, ipv6 [2606:2800:220:1:248:1893:25c8:1946]:443
```

---

## 测试（仅 E2E）

仓库 `tests/` 下仅保留端到端用例：

- `local_e2e_test.cpp`：TCP，多客户端并发、多轮消息回环
- `local_udp_e2e_test.cpp`：UDP，本地 echo + 转发校验

启用并运行全部 E2E（Linux / Ninja 示例）：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPORTFORWARDX_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

仅运行 TCP E2E：

```bash
ctest --test-dir build -R portforwardx_local_e2e_test --output-on-failure
```

仅运行 UDP E2E：

```bash
ctest --test-dir build -R portforwardx_local_udp_e2e_test --output-on-failure
```

---

## 安装与交付目录

除了 `build/` 目录直接运行，也支持 CMake 安装到独立目录（包含可执行文件和手册）：

Linux:

```bash
cmake --install build --prefix dist/install
```

Windows:

```bat
cmake --install build --config Release --prefix dist/install
```

安装目录结构示例：

- `dist/install/bin/portforwardx`（或 `portforwardx.exe`）
- `dist/install/bin/<runtime deps>`（当使用动态第三方库时）
- `dist/install/manual.md`
