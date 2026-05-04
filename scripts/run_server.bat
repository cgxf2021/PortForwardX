@echo off
setlocal EnableExtensions

if not "%PORTFORWARDX_BIN%"=="" (
  set "BIN_PATH=%PORTFORWARDX_BIN%"
) else (
  set "BIN_PATH=%~dp0portforwardx.exe"
)

if "%PUBLIC_LISTEN_HOST%"=="" set "PUBLIC_LISTEN_HOST=::"
if "%PUBLIC_LISTEN_PORT%"=="" set "PUBLIC_LISTEN_PORT=9331"
if "%LAN_TARGET_HOST%"=="" set "LAN_TARGET_HOST=127.0.0.1"
if "%LAN_TARGET_PORT%"=="" set "LAN_TARGET_PORT=8211"
if "%PROTOCOL%"=="" set "PROTOCOL=udp"
if "%FAMILY%"=="" set "FAMILY=auto"
if "%DNS_REFRESH_MIN%"=="" set "DNS_REFRESH_MIN=10"
if "%UDP_IDLE_TIMEOUT_SEC%"=="" set "UDP_IDLE_TIMEOUT_SEC=300"
set "UDP_DEBUG_ARG="
if /I "%UDP_DEBUG%"=="1" set "UDP_DEBUG_ARG=--udp-debug"
if /I "%UDP_DEBUG%"=="true" set "UDP_DEBUG_ARG=--udp-debug"
if /I "%UDP_DEBUG%"=="on" set "UDP_DEBUG_ARG=--udp-debug"

"%BIN_PATH%" ^
  --mode server ^
  --public-listen-host "%PUBLIC_LISTEN_HOST%" ^
  --public-listen-port %PUBLIC_LISTEN_PORT% ^
  --lan-target-host "%LAN_TARGET_HOST%" ^
  --lan-target-port %LAN_TARGET_PORT% ^
  --protocol %PROTOCOL% ^
  --family %FAMILY% ^
  --dns-refresh-min %DNS_REFRESH_MIN% ^
  --udp-idle-timeout-sec %UDP_IDLE_TIMEOUT_SEC% ^
  %UDP_DEBUG_ARG%

exit /b %errorlevel%
