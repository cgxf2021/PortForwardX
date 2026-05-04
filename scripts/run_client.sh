#!/usr/bin/env sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PORTFORWARDX_BIN="${PORTFORWARDX_BIN:-$SCRIPT_DIR/portforwardx}"

LOCAL_LISTEN_HOST="${LOCAL_LISTEN_HOST:-127.0.0.1}"
LOCAL_LISTEN_PORT="${LOCAL_LISTEN_PORT:-8211}"
SERVER_IPV6_HOST="${SERVER_IPV6_HOST:-minecraft.smxrfx.top}"
SERVER_PORT="${SERVER_PORT:-9331}"
PROTOCOL="${PROTOCOL:-udp}"
FAMILY="${FAMILY:-auto}"
DNS_REFRESH_MIN="${DNS_REFRESH_MIN:-10}"
UDP_IDLE_TIMEOUT_SEC="${UDP_IDLE_TIMEOUT_SEC:-300}"
UDP_DEBUG="${UDP_DEBUG:-0}"
UDP_DEBUG_ARG=""
case "$UDP_DEBUG" in
  1|true|TRUE|on|ON) UDP_DEBUG_ARG="--udp-debug" ;;
esac

exec "$PORTFORWARDX_BIN" \
  --mode client \
  --local-listen-host "$LOCAL_LISTEN_HOST" \
  --local-listen-port "$LOCAL_LISTEN_PORT" \
  --server-ipv6-host "$SERVER_IPV6_HOST" \
  --server-port "$SERVER_PORT" \
  --protocol "$PROTOCOL" \
  --family "$FAMILY" \
  --dns-refresh-min "$DNS_REFRESH_MIN" \
  --udp-idle-timeout-sec "$UDP_IDLE_TIMEOUT_SEC" \
  $UDP_DEBUG_ARG \
  "$@"
