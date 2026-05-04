param(
    [string]$LocalListenHost = "127.0.0.1",
    [int]$LocalListenPort = 8211,
    [string]$ServerIpv6Host = "minecraft.smxrfx.top",
    [int]$ServerPort = 9331,
    [ValidateSet("tcp", "udp", "both")]
    [string]$Protocol = "udp",
    [ValidateSet("auto", "ipv4", "ipv6")]
    [string]$Family = "auto",
    [int]$DnsRefreshMin = 10,
    [int]$UdpIdleTimeoutSec = 300,
    [switch]$UdpDebug
)

$binPath = if ($env:PORTFORWARDX_BIN) { $env:PORTFORWARDX_BIN } else { Join-Path $PSScriptRoot "portforwardx.exe" }
$udpDebugArg = if ($UdpDebug) { @("--udp-debug") } else { @() }

& $binPath `
    --mode client `
    --local-listen-host $LocalListenHost `
    --local-listen-port $LocalListenPort `
    --server-ipv6-host $ServerIpv6Host `
    --server-port $ServerPort `
    --protocol $Protocol `
    --family $Family `
    --dns-refresh-min $DnsRefreshMin `
    --udp-idle-timeout-sec $UdpIdleTimeoutSec `
    @udpDebugArg
