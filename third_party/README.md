# Third-Party Dependencies

本项目默认通过本目录管理第三方库，不安装到系统目录。

## Boost (ProgramOptions)

### Linux / macOS

```bash
./third_party/build_boost.sh static
```

### Windows (PowerShell)

```powershell
pwsh -ExecutionPolicy Bypass -File .\third_party\build_boost.ps1 -LinkType static
```

可选值：

- `static`：生成/安装静态库
- `shared`：生成/安装动态库

安装位置固定为：

- `third_party/install/boost`

项目配置时会通过 `BOOST_ROOT=third_party/install/boost` + `find_package(Boost ...)` 查找并链接。
