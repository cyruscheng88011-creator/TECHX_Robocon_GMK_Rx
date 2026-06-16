# techx_vision_bridge

`techx_vision_bridge` 是 GMK 端 ROS 2 视觉桥接包。它接收 Jetson 视觉端发送的 UDP V2 视觉帧，把 `camera_link` 相机坐标转换到 GMK 的固定控制坐标系，然后发布 ROS 2 话题给导航、决策、下位机通信、调试可视化等包订阅。

本仓库只保留可移植源码包：

```text
src/techx_vision_bridge
```

`build/`、`install/`、`log/` 都是本地 colcon 生成物，不属于源码。

---

## 1. 总体职责

```text
Jetson vision UDP V2
        │
        ▼
techx_vision_bridge
        │
        ├── /techx/vision/frame       主话题，一帧完整结果
        ├── /techx/vision/objects     单目标调试流
        ├── /techx/vision/kfs_targets 旧详细单目标兼容
        └── /techx/vision/targets     旧 XYZ 兼容
```

本包只做 UDP 接收、校验、目标语义映射、坐标变换和 ROS 2 发布。它不做导航、不做决策、不直接控制下位机。其他包只订阅话题，不需要解析 UDP。

---

## 2. 比赛流程与目标语义

当前流程建议按任务阶段处理：

```text
1. Head 获取阶段
   Jetson 识别 Head，GMK 转到 arm1_base，机械臂1使用。

2. Head 与 R1 对接阶段
   可以识别 QR 或后续自定义对接 marker，用于水平对准或拼接确认。

3. KFS 阶段
   识别 R2 真 KFS，GMK 转到 arm2_base，机械臂2使用。

4. QR 对齐/靠近阶段
   识别 R1 QR，用 align_err_x/y 对齐，用 robot_base/control_z 控制靠近距离。
```

默认目标映射：

| 目标 | `class_id` | `zone_id` | `target_type` | 推荐坐标系 |
|---|---|---:|---:|---|
| KFS | `0~4` | `2` | `2` | `arm2_base` |
| Head | `100~149` | `1` | `1` | `arm1_base` |
| QR | `200` | `3` | `3` | `robot_base` |

KFS 默认类别：

```text
0 fake_kfs
1 r1_kfs_red
2 r1_kfs_blue
3 r2_kfs_red
4 r2_kfs_blue
```

颜色定义：

```text
0 unknown
1 red
2 blue
```

---

## 3. 四个固定坐标系

```text
camera_link
  Jetson 相机坐标系。Jetson 下发的 x/y/z 永远是这个坐标系。

robot_base
  机器人本体坐标系。底盘移动、QR 对齐和靠近主要使用它。

arm1_base
  机械臂1基座坐标系。Head 获取/对接建议使用它。

arm2_base
  机械臂2基座坐标系。KFS 操作建议使用它。
```

外参方向采用 `T_to_from`：

```text
p_robot = T_robot_camera * p_camera
p_arm1  = T_arm1_robot  * p_robot
p_arm2  = T_arm2_robot  * p_robot
```

配置项：

```yaml
enable_transforms: true
T_robot_camera_xyz_rpy: [x, y, z, roll, pitch, yaw]
T_arm1_robot_xyz_rpy:  [x, y, z, roll, pitch, yaw]
T_arm2_robot_xyz_rpy:  [x, y, z, roll, pitch, yaw]
```

单位：平移是米，旋转是弧度。旋转顺序：

```text
R = Rz(yaw) * Ry(pitch) * Rx(roll)
p_out = R * p_in + t
```

默认外参全是 0，只能用于通信测试，不能用于实车抓取。实车必须填写真实外参，并确认方向不是反的。

---

## 4. 主话题 `/techx/vision/frame`

类型：

```text
techx_vision_bridge/msg/VisionFrame
```

一条消息代表 Jetson 的一个完整视觉帧：

```text
std_msgs/Header header
uint32 seq
uint8 protocol_version
float64 upstream_timestamp
uint8 target_count
bool has_target
techx_vision_bridge/VisionObject[] targets
```

语义：

```text
has_target=false, target_count=0
  Jetson 和 bridge 在线，但当前帧没有 fresh 目标。

has_target=true, target_count>0
  targets[] 中包含本帧所有目标。
```

每个 `VisionObject` 同时包含：

```text
目标语义：zone_id / target_type / class_id / color / confidence
像素信息：u / v / align_err_x / align_err_y
camera_link：valid_xyz / x / y / z
robot_base：valid_robot_xyz / robot_x / robot_y / robot_z
arm1_base：valid_arm1_xyz / arm1_x / arm1_y / arm1_z
arm2_base：valid_arm2_xyz / arm2_x / arm2_y / arm2_z
推荐控制：control_frame / valid_control_xyz / control_x / control_y / control_z
排序分数：priority
```

`control_frame`：

```text
0 FRAME_UNKNOWN
1 FRAME_CAMERA_LINK
2 FRAME_ROBOT_BASE
3 FRAME_ARM1_BASE
4 FRAME_ARM2_BASE
```

新决策包优先订阅 `/techx/vision/frame`。兼容话题 `/techx/vision/targets` 不能表达无目标或无深度状态，不建议作为主输入。

---

## 5. 可扩展目标映射

GMK 目标语义不再写死在 C++ 里，而是由 `config/vision_bridge.yaml` 的 `class_rules` 配置。

规则格式：

```text
"class_or_range:zone_id:target_type:control_frame:priority_bias"
```

`control_frame` 取值：

```text
1 camera_link
2 robot_base
3 arm1_base
4 arm2_base
```

默认配置：

```yaml
class_rules:
  - "0-4:2:2:4:0.0"       # KFS  -> arm2_base
  - "100-149:1:1:3:0.0"   # Head -> arm1_base
  - "200:3:3:2:0.0"       # QR   -> robot_base
```

新增目标例子：如果 Jetson 新增 docking marker，class_id 为 `150~159`，希望决策包在机器人本体坐标系使用：

```yaml
class_rules:
  - "0-4:2:2:4:0.0"
  - "100-149:1:1:3:0.0"
  - "200:3:3:2:0.0"
  - "150-159:10:10:2:0.0"
```

这样 GMK 不需要改 C++，其他包继续订阅 `/techx/vision/frame`，只需要按新的 `target_type` 或 `class_id` 筛选。

---

## 6. UDP V2 协议

Jetson 每个视觉周期发送一个 V2 包，即使没有目标也发送 `count=0`。

Header，小端序：

```c
uint16_t magic;      // 0x55AB
uint8_t  version;    // 2
uint8_t  flags;      // 0
uint32_t seq;
double   timestamp;
uint8_t  count;      // 0~16
```

每个目标，小端序：

```c
uint8_t track_id;
uint8_t class_id;
uint8_t color;
float confidence;
float u;
float v;
float x;
float y;
float z;
```

包长度：

```text
17 + count * 27 + 2
```

最后 2 字节是 CRC16-CCITT，覆盖前面所有字节。

状态定义：

```text
count=0
  当前帧无目标，但 Jetson/bridge 在线。

count>0 且 z=0
  识别到目标，但本帧没有有效 3D 距离。

count>0 且 z>0
  有有效 camera_link x/y/z，可进行 GMK 坐标变换。
```

旧 29 字节协议只作兼容，默认不接收：

```yaml
accept_legacy: false
```

---

## 7. 编译和启动

把源码包放到目标工作空间：

```text
gmk_ws/src/techx_vision_bridge
```

编译：

```bash
source /opt/ros/humble/setup.bash
cd ~/gmk_ws
rm -rf build install log
colcon build --packages-select techx_vision_bridge
source install/setup.bash
```

启动：

```bash
ros2 launch techx_vision_bridge vision_bridge.launch.py
```

配置文件：

```text
src/techx_vision_bridge/config/vision_bridge.yaml
```

YAML 顶层必须是节点名：

```yaml
vision_bridge_node:
  ros__parameters:
```

---

## 8. 模拟 Jetson 数据流

启动 bridge：

```bash
ros2 launch techx_vision_bridge vision_bridge.launch.py
```

查看主话题：

```bash
ros2 topic echo /techx/vision/frame
```

模拟 Jetson：

```bash
ros2 run techx_vision_bridge mock_jetson_sender.py --mode mixed --ip 127.0.0.1
```

支持模式：

| mode | 说明 |
|---|---|
| `empty` | V2 `count=0`，无目标状态 |
| `kfs` | 两个 KFS 目标 |
| `head` | 一个 Head 目标 |
| `qr` | 一个 QR 目标 |
| `invalid-depth` | QR 目标但 `z=0` |
| `mixed` | Head + KFS + QR 同帧 |
| `legacy` | 旧 29 字节包，仅用于兼容测试 |

---

## 9. 其他包如何订阅

### package.xml

```xml
<depend>techx_vision_bridge</depend>
```

### CMakeLists.txt

```cmake
find_package(rclcpp REQUIRED)
find_package(techx_vision_bridge REQUIRED)

ament_target_dependencies(your_node
  rclcpp
  techx_vision_bridge
)
```

### C++ 示例

```cpp
#include "rclcpp/rclcpp.hpp"
#include "techx_vision_bridge/msg/vision_frame.hpp"
#include "techx_vision_bridge/msg/vision_object.hpp"

sub_ = create_subscription<techx_vision_bridge::msg::VisionFrame>(
  "/techx/vision/frame",
  rclcpp::QoS(rclcpp::KeepLast(5)).reliable(),
  [this](techx_vision_bridge::msg::VisionFrame::SharedPtr msg) {
    if (!msg->has_target || msg->target_count == 0) {
      return;
    }

    for (const auto &obj : msg->targets) {
      if (obj.target_type == techx_vision_bridge::msg::VisionObject::TYPE_KFS) {
        // KFS 阶段：按 class_id / color / priority 选择目标，使用 control_x/y/z
      } else if (obj.target_type == techx_vision_bridge::msg::VisionObject::TYPE_QR) {
        // QR 阶段：先用 align_err_x/y 对齐，再用 control_z 靠近
      } else if (obj.target_type == techx_vision_bridge::msg::VisionObject::TYPE_WEAPON_HEAD) {
        // Head 阶段：使用 arm1/control 坐标
      }
    }
  }
);
```

---

## 10. 决策包数据选择逻辑

决策包不需要向 Jetson 请求某一个目标。Jetson 每帧主动下发所有目标，GMK 发布完整 `VisionFrame`。决策包根据当前任务阶段筛选：

| 阶段 | 过滤条件 | 使用数据 |
|---|---|---|
| Head 获取 | `target_type=TYPE_WEAPON_HEAD` | `control_x/y/z`，应为 arm1_base |
| Head/R1 对接 | QR 或自定义 marker | `align_err_x/y`、`control_z` |
| KFS 梅花林 | `target_type=TYPE_KFS`，再筛 R2 真 KFS | `control_x/y/z`，应为 arm2_base |
| QR 对齐靠近 | `target_type=TYPE_QR` | `align_err_x/y`、`control_z`，应为 robot_base |

多目标选择推荐顺序：

```text
任务阶段匹配
> class_id/color 匹配
> valid_control_xyz
> confidence
> priority
> 距离/中心误差
```

不要按接收顺序选第一个目标。

---

## 11. 当前边界

当前 V2 不携带二维码字符串内容，也不携带目标姿态。

```text
只需要 QR 中心和距离：当前够用。
需要二维码内容：需要 V3/TLV 或额外字段。
需要抓取姿态/对接角度：需要扩展 bbox_w/h、角度、法向或专用 Pose 消息。
```

---

## 12. 常见问题

### 参数没生效

检查 YAML 顶层是不是：

```yaml
vision_bridge_node:
  ros__parameters:
```

### 其他包找不到消息类型

```bash
source install/setup.bash
```

如果改过 `.msg`：

```bash
rm -rf build install log
colcon build
source install/setup.bash
```

### 有 QR 但 `/techx/vision/targets` 没数据

正常。旧 XYZ 话题只在 `valid_xyz=true` 时发布。请看 `/techx/vision/frame`。

### 收不到 UDP

```bash
ss -lunp | grep 12345
sudo ufw allow 12345/udp
```

确认 Jetson 的 `target_ip` 是 GMK 网口 IP。
