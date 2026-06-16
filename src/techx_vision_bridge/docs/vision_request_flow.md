# Decision-driven vision request flow

`techx_vision_bridge` now has two layers:

1. `vision_bridge_node`: Jetson UDP V2 -> `/techx/vision/frame`
2. `vision_selector_node`: `/techx/vision/request` + latest frame -> `/techx/vision/selected`

The raw frame topic remains the source of truth. The selector is a convenience layer that lets the decision package use vision as a smart sensor.

## Topics

| Topic | Type | Direction | Purpose |
|---|---|---|---|
| `/techx/vision/frame` | `VisionFrame` | bridge -> all packages | Complete latest vision frame |
| `/techx/vision/request` | `VisionRequest` | decision -> selector | Desired target filter |
| `/techx/vision/selected` | `VisionSelection` | selector -> decision/control | Best matching target from latest frame |

## Request examples

### Head for arm1

```text
VisionRequest:
  request_seq: 1
  target_type: 1
  zone_id: 1
  use_class_id: false
  use_color: false
  require_control_xyz: true
  min_confidence: 0.4
  max_frame_age_sec: 0.2
```

The selected target should have `control_frame=3` (`arm1_base`).

### KFS for arm2

```text
VisionRequest:
  request_seq: 2
  target_type: 2
  zone_id: 2
  use_class_id: true
  class_id: 3
  use_color: false
  require_control_xyz: true
  min_confidence: 0.4
  max_frame_age_sec: 0.2
```

The selected target should have `control_frame=4` (`arm2_base`).

### QR for robot alignment

```text
VisionRequest:
  request_seq: 3
  target_type: 3
  zone_id: 3
  use_class_id: true
  class_id: 200
  use_color: false
  require_control_xyz: false
  min_confidence: 0.3
  max_frame_age_sec: 0.2
```

For horizontal alignment, use `target.align_err_x` and `target.align_err_y` even when depth is invalid. For approach distance, require `target.valid_control_xyz=true` and use `target.control_z`.

## Status values

`VisionSelection.status`:

```text
0 STATUS_OK
1 STATUS_NO_REQUEST
2 STATUS_NO_FRAME
3 STATUS_NO_MATCH
4 STATUS_FRAME_STALE
5 STATUS_REQUEST_STALE
```

Decision code must only command motion when:

```text
has_match == true
status == STATUS_OK
```

For arm motion or QR approach, also require:

```text
target.valid_control_xyz == true
```

## Recommended architecture

The decision package should publish one request whenever the task stage changes. The request remains active until a new request is sent, unless `request_timeout_sec` is configured.

```text
Task stage: Head pickup
  publish VisionRequest(target_type=1, zone_id=1, require_control_xyz=true)
  subscribe /techx/vision/selected
  use selected.target.control_x/y/z

Task stage: KFS
  publish VisionRequest(target_type=2, zone_id=2, require_control_xyz=true)
  subscribe /techx/vision/selected
  use selected.target.control_x/y/z

Task stage: QR alignment
  publish VisionRequest(target_type=3, zone_id=3, class_id=200)
  subscribe /techx/vision/selected
  use selected.target.align_err_x/y first, then control_z when valid
```

This avoids duplicating target filtering logic across decision, navigation, and communication packages while preserving access to the complete raw `/techx/vision/frame` topic.
