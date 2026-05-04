param(
    [string]$PublicListenHost = "::",
    [int]$PublicListenPort = 9331,
    [string]$LanTargetHost = "127.0.0.1",
    [int]$LanTargetPort = 8211,
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
    --mode server `
    --public-listen-host $PublicListenHost `
    --public-listen-port $PublicListenPort `
    --lan-target-host $LanTargetHost `
    --lan-target-port $LanTargetPort `
    --protocol $Protocol `
    --family $Family `
    --dns-refresh-min $DnsRefreshMin `
    --udp-idle-timeout-sec $UdpIdleTimeoutSec `
    @udpDebugArg
