# TECHX GMK Vision Bridge 上车运行说明

本仓库运行在 **GMK / ROS2 主控机**，负责接收 Jetson UDP V2，发布 ROS2 视觉话题，并做坐标转换与标定安全保护。

固定比赛网络：

```text
Jetson IP = 192.168.10.101/24
GMK IP    = 192.168.10.100/24
UDP port  = 12345
```

数据链路：

```text
Jetson UDP V2
  -> vision_bridge_node
  -> /techx/vision/frame_raw
  -> /techx/vision/selected_raw
  -> calibration_guard_node
  -> /techx/vision/frame
  -> /techx/vision/selected
  -> 决策/底盘/机械臂
```

决策包应该使用正式话题：

```text
/techx/vision/frame
/techx/vision/selected
```

`*_raw` 话题只用于调试。

---

## 1. 直接上机运行

确认你使用的是包含本说明的分支或已合并后的 main：

```bash
git checkout fix/field-calibration-safety
```

然后在 GMK 工作空间根目录运行：

```bash
bash scripts/field_start_gmk.sh
```

这个脚本会自动执行：

```text
1. sudo 配置 GMK 有线网卡为 192.168.10.100/24
2. source ROS2 环境
3. colcon build --packages-select techx_vision_bridge
4. source install/setup.bash
5. ros2 launch techx_vision_bridge vision_bridge.launch.py
```

如果有多个有线网卡，自动选择可能会停止并要求你指定接口：

```bash
sudo bash scripts/setup_field_network.sh gmk eth0
TECHX_NET_IFACE=eth0 bash scripts/field_start_gmk.sh
```

如果网络已经由系统静态配置好，可以跳过自动配网：

```bash
TECHX_SKIP_NET_SETUP=1 bash scripts/field_start_gmk.sh
```

---

## 2. 网络配置

GMK 侧 `config/vision_bridge.yaml` 固定监听：

```yaml
udp_bind_addr: "0.0.0.0"
udp_port: 12345
```

含义：

```text
GMK 监听所有网卡的 UDP 12345
Jetson 发送端固定发到 192.168.10.100:12345
```

手动配置 GMK 网络：

```bash
sudo bash scripts/setup_field_network.sh gmk
```

指定网卡：

```bash
sudo bash scripts/setup_field_network.sh gmk eth0
```

确认 Jetson 可达：

```bash
ping 192.168.10.101
```

---

## 3. ROS2 话题

| 话题 | 类型 | 谁发布 | 谁使用 | 说明 |
|---|---|---|---|---|
| `/techx/vision/frame_raw` | `VisionFrame` | bridge | 调试 | Jetson 原始完整帧 |
| `/techx/vision/selected_raw` | `VisionSelection` | bridge | 调试 | 未经过标定保护的筛选结果 |
| `/techx/vision/frame` | `VisionFrame` | guard | 决策/调试 | 正式完整帧 |
| `/techx/vision/selected` | `VisionSelection` | guard | 决策/底盘/机械臂 | 正式筛选目标 |
| `/techx/vision/request` | `VisionRequest` | 决策包 | GMK | 请求当前阶段目标 |

上场时不要让决策包订阅 `*_raw` 话题。

---

## 4. 目标类别与坐标用途

| 阶段 | class_id | 名称 | GMK 目标坐标系 | 用途 |
|---|---:|---|---|---|
| 武器头 | 100 | `weapon_head_fist` | `arm1_base` | 机械臂1抓取武器头 |
| 武器头 | 101 | `weapon_head_palm` | `arm1_base` | 机械臂1抓取武器头 |
| 武器头 | 102 | `weapon_head_spear` | `arm1_base` | 机械臂1抓取武器头 |
| 灯条 | 150 | `r1_assembly_light_red` | `camera_link` | 拼接成功事件 |
| 灯条 | 151 | `r1_assembly_light_blue` | `camera_link` | 拼接成功事件 |
| 成功事件 | 152 | `assembly_success` | `camera_link` | 决策确认拼接成功 |
| KFS | 0~5 | KFS | `arm2_base` | 机械臂2抓取 KFS |
| 二维码 | 200 | `qr_code` | `robot_base` | 三区二维码对齐 |

`config/vision_bridge.yaml` 中 `class_rules` 决定这些映射。

---

## 5. 标定安全保护

GMK 侧有 `calibration_guard_node`。它的作用是：

```text
未标定时，不允许 robot/arm/control 坐标被误当成有效坐标。
```

默认：

```yaml
robot_camera_calibrated: false
arm1_robot_calibrated: false
arm2_robot_calibrated: false
allow_camera_link_control_when_uncalibrated: true
drop_uncalibrated_control_selection: true
```

含义：

```text
1. 灯条 class_id=150/151/152 是 camera_link/event，可以在未标定时使用。
2. 武器头 class_id=100/101/102 需要 arm1_base，未标定时正式 /selected 会被拦截。
3. KFS class_id=0~5 需要 arm2_base，未标定时正式 /selected 会被拦截。
4. 二维码 robot_base 坐标需要 T_robot_camera，未标定时不要用 robot_x/y/z 做闭环。
```

标定完成后，把 Jetson 导出的 YAML 外参复制到 `config/vision_bridge.yaml`，再把对应 flag 改成 true。

---

## 6. 比赛流程中的 request 建议

决策包必须周期性发布 `/techx/vision/request`。`request_timeout_sec` 默认 0.5s，所以建议 10Hz 以上刷新。

### 6.1 抓武器头

```text
class_id = 100 / 101 / 102
require_control_xyz = true
min_confidence >= 0.60
max_frame_age_sec <= 0.20
```

前提：

```text
robot_camera_calibrated = true
arm1_robot_calibrated = true
```

### 6.2 拼接成功灯条

```text
class_id = 152
require_control_xyz = false
min_confidence >= 0.80
max_frame_age_sec <= 0.20
```

决策包应连续 3~5 帧 OK 后才进入下一阶段。

### 6.3 抓真 R2 KFS

红方：

```text
class_id = 2
require_control_xyz = true
```

蓝方：

```text
class_id = 5
require_control_xyz = true
```

前提：

```text
robot_camera_calibrated = true
arm2_robot_calibrated = true
```

### 6.4 三区二维码对齐

先用图像误差对齐：

```text
class_id = 200
require_control_xyz = false
使用 align_err_x / align_err_y
```

完成 `T_robot_camera` 标定后，再允许使用 robot 坐标。

---

## 7. 启动后检查

看节点：

```bash
ros2 node list
```

应该有：

```text
/vision_bridge_node
/calibration_guard_node
```

看 Jetson 原始数据：

```bash
ros2 topic echo /techx/vision/frame_raw
```

看正式数据：

```bash
ros2 topic echo /techx/vision/frame
```

看筛选结果：

```bash
ros2 topic echo /techx/vision/selected
```

看频率：

```bash
ros2 topic hz /techx/vision/frame_raw
ros2 topic hz /techx/vision/frame
```

---

## 8. 常见故障

### `/frame_raw` 没数据

说明 GMK 没收到 Jetson UDP。按顺序查：

```bash
ip -br addr
ping 192.168.10.101
sudo ss -lunp | grep 12345
```

确认 Jetson 配置：

```text
local_ip  = 192.168.10.101
target_ip = 192.168.10.100
target_port = 12345
```

### `/frame_raw` 有，`/frame` 没有

检查 guard 节点是否启动：

```bash
ros2 node list | grep calibration_guard
```

### `/frame` 有，`/selected` 是 NO_REQUEST`

决策包没有发布 `/techx/vision/request`。

### `/selected` 是 REQUEST_STALE`

决策包发布频率太低。`request_timeout_sec=0.5`，建议 10Hz 以上。

### `/selected` 是 NO_MATCH`

常见原因：

```text
1. 当前帧没有请求的 class_id。
2. 目标置信度低于 request.min_confidence。
3. require_control_xyz=true，但外参未标定，guard 拦截。
4. 目标帧太旧，超过 max_frame_age_sec。
```

### `/selected_raw` OK，但 `/selected` NO_MATCH`

这是 guard 在保护你：raw 里有目标，但正式坐标未标定或不可用。

---

## 9. Mock 测试

没有 Jetson 时可以用 mock sender 测 GMK 包：

```bash
source install/setup.bash
ros2 run techx_vision_bridge mock_jetson_sender.py --mode mixed --ip 127.0.0.1
```

另开终端：

```bash
source install/setup.bash
ros2 topic echo /techx/vision/frame_raw
```

注意：mock 只验证 GMK ROS2 包，不代表真实相机、真实模型或真实 UDP 网络已经可用。

---

## 10. 上车合格标准

真正进入自动控制前，至少满足：

```text
1. GMK 网络脚本设置后 IP 是 192.168.10.100/24。
2. Jetson 能 ping 通 GMK，GMK 能 ping 通 Jetson。
3. /techx/vision/frame_raw 持续有数据。
4. /techx/vision/frame 持续有数据。
5. 灯条 class_id=152 可以在 R1 点亮时稳定出现，熄灭后消失。
6. 武器头 100/101/102 能稳定识别。
7. 真 R2 KFS class_id=2 或 5 能连续稳定。
8. 二维码 class_id=200 能输出 align_err_x/y。
9. 做机械臂抓取前，对应 calibrated flag 已经为 true。
```