#!/usr/bin/env sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PORTFORWARDX_BIN="${PORTFORWARDX_BIN:-$SCRIPT_DIR/portforwardx}"

PUBLIC_LISTEN_HOST="${PUBLIC_LISTEN_HOST:-::}"
PUBLIC_LISTEN_PORT="${PUBLIC_LISTEN_PORT:-9331}"
LAN_TARGET_HOST="${LAN_TARGET_HOST:-127.0.0.1}"
LAN_TARGET_PORT="${LAN_TARGET_PORT:-8211}"
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
  --mode server \
  --public-listen-host "$PUBLIC_LISTEN_HOST" \
  --public-listen-port "$PUBLIC_LISTEN_PORT" \
  --lan-target-host "$LAN_TARGET_HOST" \
  --lan-target-port "$LAN_TARGET_PORT" \
  --protocol "$PROTOCOL" \
  --family "$FAMILY" \
  --dns-refresh-min "$DNS_REFRESH_MIN" \
  --udp-idle-timeout-sec "$UDP_IDLE_TIMEOUT_SEC" \
  $UDP_DEBUG_ARG \
  "$@"
