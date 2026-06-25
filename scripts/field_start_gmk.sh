#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_DIR}"

NET_IFACE="${TECHX_NET_IFACE:-}"
PACKAGE="techx_vision_bridge"
JETSON_IP="192.168.10.101"
GMK_IP="192.168.10.100"
UDP_PORT="12345"

BRANCH="$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
COMMIT="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"

echo "[TECHX] GMK field startup"
echo "[TECHX] Repo    : ${REPO_DIR}"
echo "[TECHX] Branch  : ${BRANCH}"
echo "[TECHX] Commit  : ${COMMIT}"
echo "[TECHX] Network : GMK=${GMK_IP} Jetson=${JETSON_IP} UDP=${UDP_PORT} iface=${NET_IFACE:-auto}"

run_net_setup() {
  local args=(gmk)
  if [[ -n "${NET_IFACE}" ]]; then
    args+=("${NET_IFACE}")
  fi
  if [[ "${EUID}" -eq 0 ]]; then
    bash scripts/setup_field_network.sh "${args[@]}"
  else
    sudo bash scripts/setup_field_network.sh "${args[@]}"
  fi
}

if [[ "${TECHX_SKIP_NET_SETUP:-0}" != "1" ]]; then
  run_net_setup
else
  echo "[TECHX] Skip network setup because TECHX_SKIP_NET_SETUP=1"
fi

if [[ -f /opt/ros/humble/setup.bash ]]; then
  # shellcheck disable=SC1091
  source /opt/ros/humble/setup.bash
elif [[ -f /opt/ros/foxy/setup.bash ]]; then
  # shellcheck disable=SC1091
  source /opt/ros/foxy/setup.bash
else
  echo "[TECHX] ERROR: no /opt/ros/<distro>/setup.bash found. Source ROS2 before starting GMK." >&2
  exit 2
fi

if ! command -v ros2 >/dev/null 2>&1; then
  echo "[TECHX] ERROR: ros2 command not found after sourcing ROS2 environment." >&2
  exit 2
fi
if ! command -v colcon >/dev/null 2>&1; then
  echo "[TECHX] ERROR: colcon not found. Install/source ROS2 colcon before starting GMK." >&2
  exit 2
fi

if ping -c 1 -W 1 "${JETSON_IP}" >/dev/null 2>&1; then
  echo "[TECHX] OK: Jetson ${JETSON_IP} reachable"
else
  echo "[TECHX] WARN: Jetson ${JETSON_IP} not reachable yet; bridge will start and wait for UDP." >&2
fi

echo "[TECHX] Building ${PACKAGE}"
colcon build --packages-select "${PACKAGE}"

if [[ -f install/setup.bash ]]; then
  # shellcheck disable=SC1091
  source install/setup.bash
else
  echo "[TECHX] ERROR: install/setup.bash not found. Build failed or this is not the workspace root." >&2
  exit 1
fi

echo "[TECHX] Starting GMK vision bridge"
echo "[TECHX] Watch for log: first UDP frame received"
echo "[TECHX] Debug CSV: /tmp/techx_gmk_selected_debug.csv"
echo "[TECHX] Topics to check: /techx/vision/frame_raw /techx/vision/frame /techx/vision/selected_raw /techx/vision/selected"
exec ros2 launch techx_vision_bridge vision_bridge.launch.py
