# TECHX GMK Vision Bridge Field Notes

This repo runs on the GMK ROS2 controller. It receives Jetson UDP V2, publishes ROS2 vision topics, applies calibration safety checks, and writes selector debug CSV files.

## Fixed network

```text
Jetson = 192.168.10.101
GMK    = 192.168.10.100
UDP    = 12345
```

## One-command field start

```bash
git checkout fix/field-calibration-safety
TECHX_NET_IFACE=eth0 bash scripts/field_start_gmk.sh
```

The script configures the GMK IP, checks `ros2` and `colcon`, builds `techx_vision_bridge`, pings Jetson, and starts the launch file. If Jetson is not reachable, it prints WARN and keeps running so the bridge can wait for UDP.

Watch for:

```text
first UDP frame received
```

## Launch

```bash
ros2 launch techx_vision_bridge vision_bridge.launch.py
```

The launch file starts:

```text
/vision_bridge_node
/calibration_guard_node
/selection_debug_node
```

## Topics

```text
/techx/vision/frame_raw
/techx/vision/selected_raw
/techx/vision/frame
/techx/vision/selected
/techx/vision/request
```

Decision code should use `/techx/vision/frame` and `/techx/vision/selected`. Raw topics are for debug.

## Selection debug CSV

`selection_debug_node.py` writes:

```text
/tmp/techx_gmk_selected_debug.csv
```

CSV fields:

```text
time, frame_seq, request_seq, status, reason, has_match,
request_class_id, request_type, request_zone, require_xyz,
target_count, best_class_id, best_conf, best_valid_xyz,
best_valid_control_xyz, best_control_frame, frame_age_sec
```

Reasons include:

```text
OK, NO_REQUEST, NO_FRAME, FRAME_STALE, REQUEST_STALE,
NO_TARGETS, NO_MATCH_CLASS, NO_MATCH_TYPE, NO_MATCH_ZONE,
NO_MATCH_COLOR, LOW_CONFIDENCE, NO_VALID_CONTROL_XYZ,
BLOCKED_AFTER_SELECTOR_OR_GUARD, UNCALIBRATED
```

## Calibration guard

Default safe flags in `config/vision_bridge.yaml`:

```yaml
robot_camera_calibrated: false
arm1_robot_calibrated: false
arm2_robot_calibrated: false
allow_camera_link_control_when_uncalibrated: true
drop_uncalibrated_control_selection: true
```

Run this before field use:

```bash
python3 src/techx_vision_bridge/tools/check_vision_bridge_config.py
```

It reports ERROR if a calibrated flag is true while the matching `T_*_xyz_rpy` transform is still all zeros.

## Request rule

The decision package must publish `/techx/vision/request` continuously at 10Hz. Do not publish once only.

```text
Pickup/control stages: require_control_xyz=true
Light/QR image alignment stages: require_control_xyz=false
```

## Acceptance

```bash
colcon build --packages-select techx_vision_bridge
ros2 launch techx_vision_bridge vision_bridge.launch.py
```

Expected: three nodes start, first UDP frame log appears after Jetson starts, all four topics exist, and `/tmp/techx_gmk_selected_debug.csv` is created.
