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

程序参数：

- `--listen-host` 监听地址（可为 IPv4 或 IPv6，默认 `::`）
- `--listen-port` 监听端口（必填）
- `--target-host` 目标地址（可为 IPv4 / IPv6 / 域名，必填）
- `--target-port` 目标端口（必填）
- `--protocol` 传输协议：`tcp` / `udp`（默认 `tcp`）
- `--family` 地址族策略：`auto` / `ipv4` / `ipv6`（默认 `auto`）
- `--dns-refresh-sec` 目标域名解析刷新间隔（秒，默认 `30`）

### Linux 示例

```bash
./build/portforwardx --listen-host 127.0.0.1 --listen-port 18080 --target-host 1.1.1.1 --target-port 80 --protocol tcp --family ipv4
```

### Windows 示例

```bat
build\win-msvc-release\Release\portforwardx.exe --listen-host ::1 --listen-port 18443 --target-host one.one.one.one --target-port 443 --protocol tcp --family auto
```

### 地址族组合示例

- `IPv4 -> IPv4`：`--listen-host 127.0.0.1 --target-host 127.0.0.1 --family ipv4`
- `IPv6 -> IPv6`：`--listen-host ::1 --target-host ::1 --family ipv6`
- `IPv4 -> IPv6`：`--listen-host 127.0.0.1 --target-host ::1 --family auto`
- `IPv6 -> IPv4`：`--listen-host ::1 --target-host 127.0.0.1 --family auto`

### 域名解析示例

域名可以直接作为目标地址，程序会通过 DNS 解析 A/AAAA 记录：

```bash
./build/portforwardx --listen-host :: --listen-port 19000 --target-host example.com --target-port 443 --protocol tcp --family auto --dns-refresh-sec 10
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
