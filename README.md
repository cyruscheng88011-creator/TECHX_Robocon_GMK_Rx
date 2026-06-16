# techx_vision_bridge

GMK 端 ROS 2 视觉桥接包。它从 Jetson 视觉端接收 UDP V2 视觉帧，并发布 ROS 2 话题给导航、决策、下位机通信和调试节点使用。

推荐把本包放在 GMK 工作空间：

```text
gmk_ws/src/techx_vision_bridge
```

---

## 1. 功能定位

本包只负责“接收 Jetson 视觉数据并转换成 ROS 2 消息”，不做导航、不做决策、不直接控制下位机。

数据流：

```text
Jetson UDP V2 -> techx_vision_bridge -> ROS 2 topics -> decision/navigation/comm packages
```

比赛视觉结果按三类区域组织：

| 区域 | zone_id | target_type | 默认 class_id |
|---|---:|---:|---|
| Head 区域 | 1 | 1 | 100~149 |
| KFS 区域 | 2 | 2 | 0~4 |
| QR 区域 | 3 | 3 | 200 |

KFS 默认类别：

```text
0 fake_kfs
1 r1_kfs_red
2 r1_kfs_blue
3 r2_kfs_red
4 r2_kfs_blue
```

颜色：

```text
0 unknown
1 red
2 blue
```

---

## 2. 主要话题

### `/techx/vision/frame`

类型：`techx_vision_bridge/msg/VisionFrame`

这是最推荐给决策包订阅的话题。每条消息代表 Jetson 的一个视觉帧，里面包含本帧全部目标。

关键字段：

```text
seq
protocol_version
upstream_timestamp
target_count
has_target
targets[]
```

语义：

```text
has_target=false, target_count=0
  Jetson/bridge 在线，但当前帧没有 fresh 目标

has_target=true
  targets[] 中包含本帧所有目标
```

### `/techx/vision/objects`

类型：`techx_vision_bridge/msg/VisionObject`

单目标流话题，适合调试、可视化、简单节点。

关键字段：

```text
seq
target_index / target_count
zone_id / target_type / class_id / color
confidence
u / v
valid_xyz
x / y / z
align_err_x / align_err_y
priority
```

### `/techx/vision/kfs_targets`

类型：`techx_vision_bridge/msg/VisionTarget`

旧详细单目标兼容话题。新决策包不建议优先用它，因为它不是帧级数组。

### `/techx/vision/targets`

类型：`techx_vision_bridge/msg/Target3D`

旧 XYZ 坐标兼容话题。只有 `valid_xyz=true` 的目标才会发布。

---

## 3. UDP V2 协议

V2 是 Jetson 正式链路。每个推理周期发送一个包，即使没有目标也发送 `count=0`。

Header：

```c
uint16_t magic;      // 0x55AB
uint8_t  version;    // 2
uint8_t  flags;      // 0
uint32_t seq;
double   timestamp;
uint8_t  count;      // 0~16
```

每个目标：

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
  当前帧无目标，但视觉在线

count>0 且 z=0
  识别到目标，但没有有效 3D 距离

count>0 且 z>0
  有有效 x/y/z，可用于控制
```

旧 29 字节协议只作兼容，默认不接收：

```yaml
accept_legacy: false
```

---

## 4. 编译和启动

```bash
source /opt/ros/humble/setup.bash
cd ~/gmk_ws
rm -rf build install log
colcon build --packages-select techx_vision_bridge
source install/setup.bash
ros2 launch techx_vision_bridge vision_bridge.launch.py
```

配置文件：

```text
src/techx_vision_bridge/config/vision_bridge.yaml
```

注意顶层必须是节点名：

```yaml
vision_bridge_node:
  ros__parameters:
    udp_port: 12345
```

---

## 5. 模拟 Jetson 发送器

为了不接真实 Jetson 也能验证 GMK 数据流，提供：

```text
src/techx_vision_bridge/tools/mock_jetson_sender.py
```

编译 source 后运行：

```bash
ros2 run techx_vision_bridge mock_jetson_sender.py --mode mixed --ip 127.0.0.1
```

或直接运行：

```bash
python3 src/techx_vision_bridge/tools/mock_jetson_sender.py --mode mixed --ip 127.0.0.1
```

模式：

| mode | 说明 |
|---|---|
| empty | V2 count=0 |
| kfs | 两个 KFS 目标 |
| head | 一个 Head 区域目标 |
| qr | 一个 QR 目标 |
| invalid-depth | QR 目标但 z=0 |
| mixed | Head + KFS + QR 同帧 |
| legacy | 旧 29 字节包 |

验证：

```bash
ros2 launch techx_vision_bridge vision_bridge.launch.py
ros2 topic echo /techx/vision/frame
ros2 run techx_vision_bridge mock_jetson_sender.py --mode mixed --ip 127.0.0.1
```

期望 `/techx/vision/frame` 里有 `targets[]`，并且不同目标的 `zone_id/target_type/class_id` 不同。

---

## 6. 其他包如何订阅

### package.xml

```xml
<depend>techx_vision_bridge</depend>
```

### CMakeLists.txt

```cmake
find_package(techx_vision_bridge REQUIRED)
ament_target_dependencies(your_node rclcpp techx_vision_bridge)
```

### C++ 示例

```cpp
#include "techx_vision_bridge/msg/vision_frame.hpp"

sub_ = create_subscription<techx_vision_bridge::msg::VisionFrame>(
  "/techx/vision/frame",
  rclcpp::SensorDataQoS(),
  [this](techx_vision_bridge::msg::VisionFrame::SharedPtr msg) {
    if (!msg->has_target) {
      return;
    }
    for (const auto &obj : msg->targets) {
      if (obj.target_type == techx_vision_bridge::msg::VisionObject::TYPE_QR) {
        // 用 align_err_x / align_err_y 对齐，用 z 判断距离
      }
      if (obj.target_type == techx_vision_bridge::msg::VisionObject::TYPE_KFS) {
        // 按 class_id / color / priority 选择目标
      }
    }
  });
```

---

## 7. 决策包建议

不要收到一个目标就立刻动作。推荐决策包只订阅 `/techx/vision/frame`，然后按任务阶段选择目标：

```text
Head 阶段：只看 target_type=1
KFS 阶段：只看 target_type=2
QR 对齐阶段：只看 target_type=3，用 align_err_x/y
QR 靠近阶段：只看 target_type=3 且 valid_xyz=true，用 z
```

QR 对齐建议阈值：

```text
abs(align_err_x) < 0.03
abs(align_err_y) < 0.03
```

多目标时不要按接收顺序选，建议按 `priority`、`confidence`、`valid_xyz`、任务需要的 `class_id/color` 选择。

---

## 8. 常见问题

### 参数没生效

检查 YAML 顶层是不是：

```yaml
vision_bridge_node:
  ros__parameters:
```

不是包名。

### 有 QR 但 `/techx/vision/targets` 没有数据

正常。旧 XYZ 话题只在 `valid_xyz=true` 时发布。请看 `/techx/vision/frame`。

### 没有目标时没有 `/targets`

正常。无目标状态只在 `/techx/vision/frame` 中表达：

```text
has_target=false
target_count=0
```

### 收不到 UDP

检查：

```bash
ss -lunp | grep 12345
sudo ufw allow 12345/udp
```

并确认 Jetson 的 target_ip 是 GMK 网口 IP。
