#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_DIR}"

NET_IFACE="${TECHX_NET_IFACE:-}"
PACKAGE="techx_vision_bridge"

if [[ "${TECHX_SKIP_NET_SETUP:-0}" != "1" ]]; then
  if [[ "${EUID}" -eq 0 ]]; then
    bash scripts/setup_field_network.sh gmk ${NET_IFACE:+"${NET_IFACE}"}
  else
    sudo bash scripts/setup_field_network.sh gmk ${NET_IFACE:+"${NET_IFACE}"}
  fi
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
  echo "[TECHX] WARN: no /opt/ros/<distro>/setup.bash found; assuming ROS2 environment is already sourced." >&2
fi

if command -v colcon >/dev/null 2>&1; then
  echo "[TECHX] Building ${PACKAGE}"
  colcon build --packages-select "${PACKAGE}"
else
  echo "[TECHX] WARN: colcon not found; skipping build." >&2
fi

if [[ -f install/setup.bash ]]; then
  # shellcheck disable=SC1091
  source install/setup.bash
else
  echo "[TECHX] ERROR: install/setup.bash not found. Build failed or this is not the workspace root." >&2
  exit 1
fi

echo "[TECHX] Starting GMK vision bridge"
exec ros2 launch techx_vision_bridge vision_bridge.launch.py