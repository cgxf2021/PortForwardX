@echo off
setlocal EnableExtensions

if not "%PORTFORWARDX_BIN%"=="" (
  set "BIN_PATH=%PORTFORWARDX_BIN%"
) else (
  set "BIN_PATH=%~dp0portforwardx.exe"
)

if "%LOCAL_LISTEN_HOST%"=="" set "LOCAL_LISTEN_HOST=127.0.0.1"
if "%LOCAL_LISTEN_PORT%"=="" set "LOCAL_LISTEN_PORT=8211"
if "%SERVER_IPV6_HOST%"=="" set "SERVER_IPV6_HOST=minecraft.smxrfx.top"
if "%SERVER_PORT%"=="" set "SERVER_PORT=9331"
if "%PROTOCOL%"=="" set "PROTOCOL=udp"
if "%FAMILY%"=="" set "FAMILY=auto"
if "%DNS_REFRESH_MIN%"=="" set "DNS_REFRESH_MIN=10"
if "%UDP_IDLE_TIMEOUT_SEC%"=="" set "UDP_IDLE_TIMEOUT_SEC=300"
set "UDP_DEBUG_ARG="
if /I "%UDP_DEBUG%"=="1" set "UDP_DEBUG_ARG=--udp-debug"
if /I "%UDP_DEBUG%"=="true" set "UDP_DEBUG_ARG=--udp-debug"
if /I "%UDP_DEBUG%"=="on" set "UDP_DEBUG_ARG=--udp-debug"

"%BIN_PATH%" ^
  --mode client ^
  --local-listen-host "%LOCAL_LISTEN_HOST%" ^
  --local-listen-port %LOCAL_LISTEN_PORT% ^
  --server-ipv6-host "%SERVER_IPV6_HOST%" ^
  --server-port %SERVER_PORT% ^
  --protocol %PROTOCOL% ^
  --family %FAMILY% ^
  --dns-refresh-min %DNS_REFRESH_MIN% ^
  --udp-idle-timeout-sec %UDP_IDLE_TIMEOUT_SEC% ^
  %UDP_DEBUG_ARG%

exit /b %errorlevel%
