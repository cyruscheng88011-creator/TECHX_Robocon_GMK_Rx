# techx_vision_bridge

`techx_vision_bridge` 是 GMK 端的 ROS 2 视觉桥接包。它只负责一件事：**接收 Jetson 视觉端发送的 UDP V2 视觉帧，把 camera_link 相机坐标转换到 GMK 需要的固定坐标系，然后发布 ROS 2 话题给导航、决策、下位机通信、调试可视化等其他包订阅。**

本仓库只保留可移植源码包：

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
        ├── /techx/vision/frame      推荐给决策包使用，一帧完整结果
        ├── /techx/vision/objects    单目标调试流
        ├── /techx/vision/kfs_targets 兼容详细单目标流
        └── /techx/vision/targets    兼容旧 XYZ 话题
```

---

## 2. 比赛流程与视觉目标

实际流程不是简单“三个区域同时处理”，而是有任务阶段：

```text
1. Head 获取阶段
   Jetson 识别 Head，GMK 将目标转换到 arm1_base，机械臂1使用。

2. Head 与 R1 对接阶段
   可能识别 R1 QR 或其他对接特征，用于水平对准或判断对接是否成功。

3. KFS 阶段
   在对应区域识别 R2 真 KFS，GMK 将 KFS 转换到 arm2_base，机械臂2使用。

4. QR 对齐/靠近阶段
   识别 R1 QR，用 align_err_x/y 做水平对齐，用 robot_base 下的距离控制机器人靠近。
```

视觉端目前按三类目标组织：

| 目标 | `zone_id` | `target_type` | 默认 `class_id` | 推荐控制坐标系 |
|---|---:|---:|---|---|
| Head | `1` | `1` | `100~149` | `arm1_base` |
| KFS | `2` | `2` | `0~4` | `arm2_base` |
| QR | `3` | `3` | `200` | `robot_base` |

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

本包按四个固定坐标系组织数据：

```text
camera_link
  Jetson 相机坐标系。相机固定在机器人正前方某个位置。Jetson 下发的 x/y/z 永远是 camera_link。

robot_base
  机器人本体坐标系。用于底盘/本体移动，QR 对齐和靠近主要使用这个坐标系。

arm1_base
  机械臂1基座坐标系。Head 获取/对接相关目标建议使用这个坐标系。

arm2_base
  机械臂2基座坐标系。KFS 相关目标建议使用这个坐标系。
```

外参方向约定采用 `T_to_from`：

```text
p_robot = T_robot_camera * p_camera
p_arm1  = T_arm1_robot  * p_robot
p_arm2  = T_arm2_robot  * p_robot
```

配置文件里对应：

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

默认值全是 0，只是单位变换。实车要使用机械测量或标定得到真实外参。

---

## 4. Jetson 与 GMK 的职责边界

Jetson 端负责：

```text
1. 相机采集和 RGB-D 对齐
2. 目标识别和二维码/特征检测
3. 深度查询
4. 输出 camera_link 下的 x/y/z
5. 输出 u/v 像素中心、confidence、class_id、color
6. 通过 UDP V2 下发一帧多目标结果
```

GMK `techx_vision_bridge` 负责：

```text
1. 接收 UDP V2
2. 校验 magic、长度、CRC、seq
3. class_id -> target_type / zone_id 映射
4. camera_link -> robot_base / arm1_base / arm2_base 坐标变换
5. 发布 /techx/vision/frame 给其他包订阅
```

不建议把机械臂1、机械臂2、机器人本体外参写死在 Jetson 端。Jetson 只下发相机坐标，GMK 统一做控制坐标转换。

---

## 5. 推荐订阅话题

### 5.1 `/techx/vision/frame`

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

### 5.2 `VisionObject` 目标字段

每个目标同时包含相机坐标、机器人坐标、机械臂坐标和推荐控制坐标：

```text
目标语义：
  zone_id / target_type / class_id / color / confidence

像素信息：
  u / v / align_err_x / align_err_y

camera_link 原始坐标：
  valid_xyz / x / y / z

robot_base 坐标：
  valid_robot_xyz / robot_x / robot_y / robot_z

arm1_base 坐标：
  valid_arm1_xyz / arm1_x / arm1_y / arm1_z

arm2_base 坐标：
  valid_arm2_xyz / arm2_x / arm2_y / arm2_z

推荐控制坐标：
  control_frame / valid_control_xyz / control_x / control_y / control_z
```

`control_frame` 定义：

```text
0 FRAME_UNKNOWN
1 FRAME_CAMERA_LINK
2 FRAME_ROBOT_BASE
3 FRAME_ARM1_BASE
4 FRAME_ARM2_BASE
```

默认推荐：

```text
Head -> control_frame=FRAME_ARM1_BASE
KFS  -> control_frame=FRAME_ARM2_BASE
QR   -> control_frame=FRAME_ROBOT_BASE
```

### 5.3 兼容话题

```text
/techx/vision/objects      techx_vision_bridge/msg/VisionObject
/techx/vision/kfs_targets  techx_vision_bridge/msg/VisionTarget
/techx/vision/targets      techx_vision_bridge/msg/Target3D
```

新决策包建议优先订阅 `/techx/vision/frame`。`/techx/vision/targets` 只有 `valid_xyz=true` 的目标才会发布，不能表达无目标或无深度状态。

---

## 6. UDP V2 协议

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
  有有效 camera_link x/y/z，可进行 GMK 坐标变换。
```

旧 29 字节协议只作兼容，默认不接收：

```yaml
accept_legacy: false
```

比赛主链路应使用 V2。

---

## 7. 移植到其他 GMK 工程

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

## 8. 编译和启动

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

## 9. 模拟 Jetson 发送器

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
| `head` | 一个 Head 目标 |
| `qr` | 一个 QR 目标 |
| `invalid-depth` | QR 目标但 `z=0` |
| `mixed` | Head + KFS + QR 同帧 |
| `legacy` | 旧 29 字节包，仅用于兼容测试 |

---

## 10. 其他包如何订阅

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

### C++ 订阅示例

```cpp
#include "rclcpp/rclcpp.hpp"
#include "techx_vision_bridge/msg/vision_frame.hpp"
#include "techx_vision_bridge/msg/vision_object.hpp"

sub_ = create_subscription<techx_vision_bridge::msg::VisionFrame>(
  "/techx/vision/frame",
  rclcpp::SensorDataQoS(),
  [this](techx_vision_bridge::msg::VisionFrame::SharedPtr msg) {
    if (!msg->has_target || msg->target_count == 0) {
      return;
    }

    for (const auto &obj : msg->targets) {
      if (obj.target_type == techx_vision_bridge::msg::VisionObject::TYPE_KFS) {
        // 使用 obj.control_frame == FRAME_ARM2_BASE 的 control_x/y/z
      } else if (obj.target_type == techx_vision_bridge::msg::VisionObject::TYPE_QR) {
        // 使用 align_err_x/y 对齐，使用 robot/control z 靠近
      } else if (obj.target_type == techx_vision_bridge::msg::VisionObject::TYPE_WEAPON_HEAD) {
        // 使用 obj.control_frame == FRAME_ARM1_BASE 的 control_x/y/z
      }
    }
  }
);
```

---

## 11. 决策包处理建议

不要写成“收到一个目标就立刻动作”。推荐流程：

```text
1. 订阅 /techx/vision/frame
2. 每次收到一帧，先看 has_target / target_count
3. 根据当前任务阶段筛选 target_type
4. 根据 valid_control_xyz、confidence、priority、class_id/color 选 best target
5. 再把 best target 交给导航或下位机通信包
```

任务阶段示例：

```text
Head 阶段：只看 TYPE_WEAPON_HEAD，使用 control_x/y/z，control_frame 应为 FRAME_ARM1_BASE
KFS 阶段：只看 TYPE_KFS，使用 control_x/y/z，control_frame 应为 FRAME_ARM2_BASE
QR 对齐阶段：只看 TYPE_QR，用 align_err_x/y
QR 靠近阶段：只看 TYPE_QR 且 valid_control_xyz=true，用 control_z，control_frame 应为 FRAME_ROBOT_BASE
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
valid_control_xyz
任务需要的 class_id/color
与图像中心的距离
control_z 距离
```

---

## 12. 仍需实车完成的标定

本包提供坐标变换能力，但不会自动知道真实外参。实车需要确认：

```text
T_robot_camera_xyz_rpy
  camera_link -> robot_base

T_arm1_robot_xyz_rpy
  robot_base -> arm1_base

T_arm2_robot_xyz_rpy
  robot_base -> arm2_base
```

如果这些外参仍为 0，所有坐标会等价于单位变换，不能作为精确机械臂抓取依据。

---

## 13. 常见问题

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
