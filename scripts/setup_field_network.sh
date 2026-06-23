#!/usr/bin/env bash
set -euo pipefail

ROLE="${1:-gmk}"
IFACE="${2:-${TECHX_NET_IFACE:-}}"

JETSON_IP="192.168.10.101"
GMK_IP="192.168.10.100"
CIDR="24"
PORT="12345"

usage() {
  cat <<'USAGE'
Usage:
  sudo bash scripts/setup_field_network.sh gmk [interface]

Defaults:
  gmk IP    = 192.168.10.100/24
  peer      = 192.168.10.101
  UDP port  = 12345

If [interface] is omitted, the script auto-selects a wired interface with carrier.
You can also set TECHX_NET_IFACE=eth0.
USAGE
}

if [[ "${ROLE}" == "-h" || "${ROLE}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ "${ROLE}" != "gmk" ]]; then
  echo "ERROR: this script is for the GMK repo; role must be 'gmk'." >&2
  usage
  exit 2
fi

if [[ "${EUID}" -ne 0 ]]; then
  echo "ERROR: network setup needs sudo/root." >&2
  echo "Run: sudo bash scripts/setup_field_network.sh gmk [interface]" >&2
  exit 1
fi

is_candidate_iface() {
  local name="$1"
  [[ "${name}" == "lo" ]] && return 1
  [[ "${name}" == wl* ]] && return 1
  [[ "${name}" == docker* ]] && return 1
  [[ "${name}" == br-* ]] && return 1
  [[ "${name}" == veth* ]] && return 1
  [[ "${name}" == virbr* ]] && return 1
  [[ "${name}" == tailscale* ]] && return 1
  [[ "${name}" == tun* ]] && return 1
  return 0
}

pick_iface() {
  local found=()
  for p in /sys/class/net/*; do
    local n
    n="$(basename "$p")"
    is_candidate_iface "$n" || continue
    if [[ -r "$p/carrier" ]] && [[ "$(cat "$p/carrier" 2>/dev/null || echo 0)" == "1" ]]; then
      found+=("$n")
    fi
  done
  if [[ "${#found[@]}" -eq 1 ]]; then
    echo "${found[0]}"
    return 0
  fi
  if [[ "${#found[@]}" -gt 1 ]]; then
    echo "ERROR: multiple wired interfaces with carrier: ${found[*]}" >&2
    echo "Pass one explicitly, e.g. sudo bash scripts/setup_field_network.sh gmk eth0" >&2
    exit 3
  fi
  for p in /sys/class/net/*; do
    local n
    n="$(basename "$p")"
    is_candidate_iface "$n" || continue
    found+=("$n")
  done
  if [[ "${#found[@]}" -eq 1 ]]; then
    echo "${found[0]}"
    return 0
  fi
  echo "ERROR: could not auto-select a wired interface." >&2
  echo "Available interfaces:" >&2
  ip -br link >&2
  echo "Pass one explicitly, e.g. sudo bash scripts/setup_field_network.sh gmk eth0" >&2
  exit 3
}

if [[ -z "${IFACE}" ]]; then
  IFACE="$(pick_iface)"
fi

if ! ip link show "${IFACE}" >/dev/null 2>&1; then
  echo "ERROR: interface '${IFACE}' does not exist." >&2
  ip -br link >&2
  exit 4
fi

echo "[TECHX] Configuring GMK field network"
echo "[TECHX] Interface : ${IFACE}"
echo "[TECHX] GMK IP    : ${GMK_IP}/${CIDR}"
echo "[TECHX] Jetson IP : ${JETSON_IP}"
echo "[TECHX] UDP port  : ${PORT}"

ip link set dev "${IFACE}" up
ip addr flush dev "${IFACE}"
ip addr add "${GMK_IP}/${CIDR}" dev "${IFACE}"

# Route is explicit for direct Jetson<->GMK link; no gateway is required.
ip route replace "192.168.10.0/24" dev "${IFACE}" src "${GMK_IP}"

ip -br addr show "${IFACE}"

echo "[TECHX] Network configured. Testing peer ping..."
if ping -c 1 -W 1 "${JETSON_IP}" >/dev/null 2>&1; then
  echo "[TECHX] OK: Jetson ${JETSON_IP} is reachable."
else
  echo "[TECHX] WARN: Jetson ${JETSON_IP} is not reachable yet. Check Jetson power/cable/IP/firewall." >&2
fi

echo "[TECHX] Done. Start GMK vision bridge before Jetson vision."