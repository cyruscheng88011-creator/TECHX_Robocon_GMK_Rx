# TECHX GMK Vision Bridge 说明

本仓库运行在 **GMK / ROS2 控制端**，负责接收 Jetson UDP V2、发布 ROS2 视觉话题、完成坐标变换、做标定安全保护，并根据 `/techx/vision/request` 输出 `/techx/vision/selected`。

Jetson 负责“看见目标并给出 camera_link 坐标”；GMK 负责“把 camera_link 转成 robot_base / arm1_base / arm2_base，并给决策包可用的 control_x/y/z”。

## 比赛流程

```text
1. R2 到武器头区：识别武器头，先视觉居中，再用 arm1_base 坐标让机械臂1抓取。
2. R2 到 R1：R1 拼接成功亮灯带，R2 识别灯带/拼接成功事件。
3. R2 进梅林：识别真 R2 KFS，先视觉居中，再用 arm2_base 坐标让机械臂2抓取。
4. R2 到三区：识别 R1 二维码，使用 align_err_x/y 边走边对齐进入 R1 抬升机构。
5. R1 抬升 R2，R2 将 KFS 放入九宫格。
```

## 固定网络

```text
Jetson = 192.168.10.101
GMK    = 192.168.10.100
UDP    = 12345
```

## 坐标系

```text
camera_link：Jetson RGB-D 相机坐标，由 Jetson UDP 直接发送。
robot_base ：机器人底盘坐标，由 T_robot_camera 转换得到。
arm1_base  ：机械臂1坐标，武器头抓取使用。
arm2_base  ：机械臂2坐标，KFS 抓取使用。
```

默认 class 规则：

```text
0-5     KFS      -> arm2_base
100-102 武器头   -> arm1_base
150-152 灯带事件 -> camera_link
200     二维码   -> robot_base
```

## 启动

```bash
git checkout fix/field-calibration-safety
TECHX_NET_IFACE=eth0 bash scripts/field_start_gmk.sh
```

脚本会配置 GMK IP、检查 ROS2/colcon、构建 `techx_vision_bridge`、ping Jetson，并启动：

```text
/vision_bridge_node
/calibration_guard_node
/selection_debug_node
```

## 话题

```text
/techx/vision/frame_raw      # 原始 UDP 解码，仅调试
/techx/vision/selected_raw   # 原始选择结果，仅调试
/techx/vision/frame          # 正式 guarded 视觉帧
/techx/vision/selected       # 正式 guarded 选择结果
/techx/vision/request        # 决策包目标请求
```

决策包只使用：

```text
/techx/vision/frame
/techx/vision/selected
```

不要在正式流程里直接用 raw 话题。

## 应用 Jetson 标定结果

Jetson 棋盘格标定会输出 `gmk_robot_camera.yaml`。复制到 GMK 后执行：

```bash
python3 src/techx_vision_bridge/tools/apply_calibration_yaml.py \
  --config src/techx_vision_bridge/config/vision_bridge.yaml \
  --snippet /path/to/gmk_robot_camera.yaml
```

同理可应用：

```text
gmk_arm1_robot.yaml
gmk_arm2_robot.yaml
```

应用后检查配置：

```bash
python3 src/techx_vision_bridge/tools/check_vision_bridge_config.py \
  --config src/techx_vision_bridge/config/vision_bridge.yaml
```

检查通过后再启动 GMK。

## 决策请求规则

决策包必须持续发布 `/techx/vision/request`，建议 10Hz，不要只发一次。

```text
搜索/居中：require_control_xyz=false
抓取阶段：require_control_xyz=true
灯带/二维码：require_control_xyz=false
```

调试命令：

```bash
ros2 run techx_vision_bridge vision_request_demo.py --name head_center
ros2 run techx_vision_bridge vision_request_demo.py --name head_fist_grasp
ros2 run techx_vision_bridge vision_request_demo.py --name kfs_center
ros2 run techx_vision_bridge vision_request_demo.py --name kfs_red_r2_true_grasp
ros2 run techx_vision_bridge vision_request_demo.py --name qr_align
ros2 run techx_vision_bridge vision_request_demo.py --name assembly_success
```

## 调试 CSV

`selection_debug_node.py` 输出：

```text
/tmp/techx_gmk_selected_debug.csv
```

重点看：

```text
status, reason, has_match, require_xyz,
best_class_id, best_conf, best_valid_control_xyz,
best_control_frame, frame_age_sec
```

常见 reason：

```text
OK
NO_REQUEST
NO_FRAME
NO_TARGETS
LOW_CONFIDENCE
NO_VALID_CONTROL_XYZ
UNCALIBRATED
FRAME_STALE
REQUEST_STALE
```

## 现场验收

```text
1. GMK 能启动三个节点。
2. Jetson 启动后 GMK 收到 first UDP frame。
3. check_vision_bridge_config.py 通过。
4. /techx/vision/selected 能输出 expected control_frame。
5. 武器头抓取使用 arm1_base。
6. KFS 抓取使用 arm2_base。
7. 二维码对齐使用 align_err_x/y。
8. 灯带只作为事件确认，不作为抓取坐标。
```
