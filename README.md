# techx_vision_bridge

`techx_vision_bridge` 是 GMK 端的 ROS 2 视觉桥接包。它只负责一件事：**接收 Jetson 视觉端发送的 UDP V2 视觉帧，并发布标准 ROS 2 话题给导航、决策、下位机通信、调试可视化等其他包订阅。**

本仓库现在只保留可移植源码包：

```text
src/techx_vision_bridge
```

`build/`、`install/`、`log/` 都是本地 colcon 生成物，不属于源码，不应提交或移植。

---

## 1. 包的定位

GMK 工程里通常会有多个包，例如：

```text
gmk_ws/src/
  techx_vision_bridge/      # 本包：Jetson UDP -> ROS 2 topics
  decision_pkg/             # 决策包：订阅视觉结果并选择任务目标
  navigation_pkg/           # 导航包：根据决策结果移动
  lower_comm_pkg/           # 下位机通信包：发送执行指令
```

本包不做路径规划、不做任务决策、不直接控制下位机。其他包只需要订阅本包发布的话题，不需要自己解析 UDP。

数据流：

```text
Jetson vision UDP V2
        │
        ▼
techx_vision_bridge
        │
        ├── /techx/vision/frame      推荐给决策包使用
        ├── /techx/vision/objects    单目标调试流
        ├── /techx/vision/kfs_targets 兼容详细单目标流
        └── /techx/vision/targets    兼容旧 XYZ 话题
```

---

## 2. 三类视觉区域

视觉端目前按三类区域组织目标：

| 区域 | `zone_id` | `target_type` | 默认 `class_id` |
|---|---:|---:|---|
| Head 区域 | `1` | `1` | `100~149` |
| KFS 区域 | `2` | `2` | `0~4` |
| QR 区域 | `3` | `3` | `200` |

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

`zone_id` 表示属于哪个任务区域，`target_type` 表示目标类型，`class_id` 是目标类型内部的类别编号。决策包应优先按 `target_type` 和当前任务阶段筛选目标，不要只看 `class_id`。

---

## 3. 推荐订阅话题

### 3.1 `/techx/vision/frame`

类型：

```text
techx_vision_bridge/msg/VisionFrame
```

这是**推荐给决策包订阅的主话题**。一条消息代表 Jetson 的一个完整视觉帧，里面包含本帧全部目标。

主要字段：

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
  Jetson 和 bridge 正常在线，但当前视觉帧没有 fresh 目标。

has_target=true, target_count>0
  targets[] 里包含本帧全部目标。
```

决策包建议只订阅这个话题，然后在一帧内统一筛选目标。

### 3.2 `/techx/vision/objects`

类型：

```text
techx_vision_bridge/msg/VisionObject
```

单目标流话题。每个目标发布一条，适合调试、可视化、简单节点，不建议作为复杂决策的唯一输入。

关键字段：

```text
seq
目标索引：target_index / target_count
目标语义：zone_id / target_type / class_id / color
检测信息：confidence / u / v
三维信息：valid_xyz / x / y / z
对齐误差：align_err_x / align_err_y
排序分数：priority
```

### 3.3 兼容话题

```text
/techx/vision/kfs_targets  techx_vision_bridge/msg/VisionTarget
/techx/vision/targets      techx_vision_bridge/msg/Target3D
```

这两个用于兼容旧节点。新决策包不建议优先使用它们。

`/techx/vision/targets` 只有 `valid_xyz=true` 的目标才会发布，所以它不能表达“无目标”或“识别到但无深度”。无目标状态请看 `/techx/vision/frame`。

---

## 4. UDP V2 协议

Jetson 正式链路使用 UDP V2 包。每个推理周期发送一个 V2 包，即使没有目标也发送 `count=0`。

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
  有有效 x/y/z，可用于后续控制。
```

旧 29 字节协议只作兼容，默认不接收：

```yaml
accept_legacy: false
```

比赛主链路应使用 V2。

---

## 5. 移植到其他 GMK 工程

只需要拷贝这个目录：

```text
src/techx_vision_bridge
```

目标工程结构示例：

```text
gmk_ws/
  src/
    techx_vision_bridge/
      CMakeLists.txt
      package.xml
      msg/
      src/
      launch/
      config/
      tools/
    decision_pkg/
    navigation_pkg/
    lower_comm_pkg/
```

不要拷贝：

```text
build/
install/
log/
```

这些目录应该由目标工程自己 `colcon build` 生成。

---

## 6. 编译和启动

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

配置文件顶层必须是节点名：

```yaml
vision_bridge_node:
  ros__parameters:
    udp_bind_addr: "0.0.0.0"
    udp_port: 12345
```

如果 launch 里改了节点名，YAML 顶层也必须同步修改。

---

## 7. 模拟 Jetson 发送器

为了在没有真实 Jetson、相机、模型的情况下验证 GMK 数据流，本包提供模拟发送器：

```text
src/techx_vision_bridge/tools/mock_jetson_sender.py
```

启动 bridge：

```bash
ros2 launch techx_vision_bridge vision_bridge.launch.py
```

查看主话题：

```bash
ros2 topic echo /techx/vision/frame
```

模拟 Jetson 发送 mixed 数据：

```bash
ros2 run techx_vision_bridge mock_jetson_sender.py --mode mixed --ip 127.0.0.1
```

也可以直接运行脚本：

```bash
python3 src/techx_vision_bridge/tools/mock_jetson_sender.py --mode mixed --ip 127.0.0.1
```

支持模式：

| mode | 说明 |
|---|---|
| `empty` | V2 `count=0`，无目标状态 |
| `kfs` | 两个 KFS 目标 |
| `head` | 一个 Head 区域目标 |
| `qr` | 一个 QR 目标 |
| `invalid-depth` | QR 目标但 `z=0` |
| `mixed` | Head + KFS + QR 同帧 |
| `legacy` | 旧 29 字节包，仅用于兼容测试 |

验证期望：

```text
empty:
  /techx/vision/frame -> has_target=false, target_count=0

mixed:
  /techx/vision/frame -> targets[] 中同时包含 Head/KFS/QR

invalid-depth:
  QR 目标 valid_xyz=false, z=0
```

---

## 8. 其他包如何订阅

### 8.1 package.xml

```xml
<depend>techx_vision_bridge</depend>
```

### 8.2 CMakeLists.txt

```cmake
find_package(rclcpp REQUIRED)
find_package(techx_vision_bridge REQUIRED)

ament_target_dependencies(your_node
  rclcpp
  techx_vision_bridge
)
```

### 8.3 C++ 订阅示例

```cpp
#include "rclcpp/rclcpp.hpp"
#include "techx_vision_bridge/msg/vision_frame.hpp"
#include "techx_vision_bridge/msg/vision_object.hpp"

sub_ = create_subscription<techx_vision_bridge::msg::VisionFrame>(
  "/techx/vision/frame",
  rclcpp::SensorDataQoS(),
  [this](techx_vision_bridge::msg::VisionFrame::SharedPtr msg) {
    if (!msg->has_target || msg->target_count == 0) {
      // 视觉在线，但当前帧没有目标
      return;
    }

    for (const auto &obj : msg->targets) {
      if (obj.target_type == techx_vision_bridge::msg::VisionObject::TYPE_KFS) {
        // KFS 阶段：按 class_id / color / priority 选择目标
      } else if (obj.target_type == techx_vision_bridge::msg::VisionObject::TYPE_QR) {
        // QR 阶段：用 align_err_x / align_err_y 对齐，用 z 判断距离
      } else if (obj.target_type == techx_vision_bridge::msg::VisionObject::TYPE_WEAPON_HEAD) {
        // Head 阶段：处理 Head 区域目标
      }
    }
  }
);
```

### 8.4 Python 订阅示例

```python
import rclpy
from rclpy.node import Node
from techx_vision_bridge.msg import VisionFrame, VisionObject

class DecisionNode(Node):
    def __init__(self):
        super().__init__("decision_node")
        self.sub = self.create_subscription(
            VisionFrame,
            "/techx/vision/frame",
            self.cb,
            10,
        )

    def cb(self, msg: VisionFrame):
        if not msg.has_target or msg.target_count == 0:
            return
        for obj in msg.targets:
            if obj.target_type == VisionObject.TYPE_KFS:
                pass
            elif obj.target_type == VisionObject.TYPE_QR:
                pass
            elif obj.target_type == VisionObject.TYPE_WEAPON_HEAD:
                pass

rclpy.init()
node = DecisionNode()
rclpy.spin(node)
rclpy.shutdown()
```

---

## 9. 决策包处理建议

不要写成“收到一个目标就立刻动作”。推荐流程：

```text
1. 订阅 /techx/vision/frame
2. 每次收到一帧，先看 has_target / target_count
3. 根据当前任务阶段筛选 target_type
4. 根据 valid_xyz、confidence、priority、class_id/color 选 best target
5. 再把 best target 交给导航或下位机通信包
```

任务阶段示例：

```text
Head 阶段：只看 target_type=TYPE_WEAPON_HEAD
KFS 阶段：只看 target_type=TYPE_KFS
QR 对齐阶段：只看 target_type=TYPE_QR，用 align_err_x/y
QR 靠近阶段：只看 target_type=TYPE_QR 且 valid_xyz=true，用 z
```

QR 对齐建议：

```text
abs(align_err_x) < 0.03
abs(align_err_y) < 0.03
```

多目标时不要按接收顺序选，建议综合：

```text
priority
confidence
valid_xyz
任务需要的 class_id/color
与图像中心的距离
z 距离
```

---

## 10. 常见问题

### 参数没生效

检查 YAML 顶层是不是节点名：

```yaml
vision_bridge_node:
  ros__parameters:
```

不是包名。

### 其他包找不到消息类型

确认已经执行：

```bash
source install/setup.bash
```

如果改过 `.msg`，建议重新清理编译：

```bash
rm -rf build install log
colcon build
source install/setup.bash
```

### 有 QR 目标但 `/techx/vision/targets` 没数据

正常。旧 XYZ 话题只在 `valid_xyz=true` 时发布。请以 `/techx/vision/frame` 为主。

### 没有目标时没有 `/techx/vision/targets`

正常。无目标状态只在 `/techx/vision/frame` 表达：

```text
has_target=false
target_count=0
```

### 收不到 UDP

检查端口：

```bash
ss -lunp | grep 12345
sudo ufw allow 12345/udp
```

确认 Jetson 的 `target_ip` 是 GMK 网口 IP。

---

## 11. 当前源码文件

仓库清理后只保留必要源码：

```text
README.md
.gitignore
src/techx_vision_bridge/CMakeLists.txt
src/techx_vision_bridge/package.xml
src/techx_vision_bridge/msg/*.msg
src/techx_vision_bridge/src/vision_frame_bridge_node.cpp
src/techx_vision_bridge/launch/vision_bridge.launch.py
src/techx_vision_bridge/config/vision_bridge.yaml
src/techx_vision_bridge/tools/mock_jetson_sender.py
```

旧节点源码、旧测试脚本、`build/`、`install/`、`log/` 已不属于当前工程结构。
