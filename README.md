# techx_vision_bridge

`techx_vision_bridge` 是 GMK 端的 ROS 2 视觉桥接包。它只负责：**接收 Jetson 视觉端 UDP V2 视觉帧，完成必要的坐标系转换，然后发布 ROS 2 话题给导航、决策、下位机通信、调试可视化等其他包订阅。**

本仓库现在只保留可移植源码包：

```text
src/techx_vision_bridge
```

`build/`、`install/`、`log/` 都是本地 colcon 生成物，不属于源码，不应提交或移植。

---

## 1. 包的定位

GMK 工程里通常会有多个包：

```text
gmk_ws/src/
  techx_vision_bridge/      # 本包：Jetson UDP -> ROS 2 topics
  decision_pkg/             # 决策包：订阅视觉结果并选择任务目标
  navigation_pkg/           # 导航包：根据决策结果移动
  lower_comm_pkg/           # 下位机通信包：发送执行指令
```

本包不做任务决策、不做路径规划、不直接控制下位机。其他包只需要订阅本包发布的话题，不需要自己解析 UDP。

数据流：

```text
Jetson vision UDP V2
        │ camera-frame targets
        ▼
techx_vision_bridge
        │ frame/object topics with camera + control coordinates
        ├── /techx/vision/frame       推荐给决策包使用
        ├── /techx/vision/objects     单目标调试流
        ├── /techx/vision/kfs_targets 兼容详细单目标流
        └── /techx/vision/targets     兼容旧 XYZ 话题
```

---

## 2. 比赛任务流程和目标类型

当前比赛视觉大流程按任务阶段理解，不应只按图像区域粗暴处理：

```text
阶段 A：机械臂1拿 Head
  识别 Head 区域目标，输出机械臂1坐标系下的控制坐标。

阶段 B：Head 与 R1 对接/拼接判断
  可能识别 R1 QR 做水平对准，也可能识别其他视觉特征判断拼接是否成功。

阶段 C：梅花林识别 KFS
  主要识别 R2 真 KFS，下发 KFS 类别/颜色/位置，给机械臂2使用。

阶段 D：三区识别 R1 QR
  用 QR 的像素误差做水平对齐，用 QR 距离 z 控制机器人靠近。
```

目标类型约定：

| 任务目标 | `zone_id` | `target_type` | 默认 `class_id` | 推荐控制坐标系 |
|---|---:|---:|---|---|
| Head | `1` | `1` | `100~149` | 机械臂1坐标系 |
| KFS | `2` | `2` | `0~4` | 机械臂2坐标系 |
| QR | `3` | `3` | `200` | 机器人本体坐标系 |

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

决策包必须根据当前任务阶段筛选 `target_type`，不要“看到什么就处理什么”。

---

## 3. 坐标系设计

相机安装在 R2 机器人正前方，因此 Jetson 解算出来的 `x/y/z` 首先是**相机坐标系**下的坐标。比赛控制至少需要三个使用坐标系：

```text
camera_link        Jetson 相机坐标系，原始视觉测距结果
robot_base         机器人本体坐标系，用于 QR 对齐/靠近/底盘移动
arm1_base          机械臂1坐标系，用于 Head 抓取/对接
arm2_base          机械臂2坐标系，用于 KFS 抓取/操作
```

本包内部按以下固定外参做转换：

```text
point_robot = T_robot_camera * point_camera
point_arm1  = T_arm1_robot  * point_robot
point_arm2  = T_arm2_robot  * point_robot
```

配置文件参数：

```yaml
T_robot_camera_xyz_rpy: [x, y, z, roll, pitch, yaw]
T_arm1_robot_xyz_rpy:  [x, y, z, roll, pitch, yaw]
T_arm2_robot_xyz_rpy:  [x, y, z, roll, pitch, yaw]
```

含义：

```text
T_robot_camera_xyz_rpy
  把相机坐标点转换到机器人本体坐标系。

T_arm1_robot_xyz_rpy
  把机器人本体坐标点转换到机械臂1坐标系。

T_arm2_robot_xyz_rpy
  把机器人本体坐标点转换到机械臂2坐标系。

roll/pitch/yaw 单位是 rad，旋转顺序是 Rz(yaw) * Ry(pitch) * Rx(roll)。
```

默认这些外参都是 0，因此默认转换是单位变换。实车上必须根据安装位置进行标定。

### Jetson 还需要做手眼标定吗？

建议职责划分如下：

```text
Jetson 端：
  负责相机内参、RGB-D 对齐、深度修正、目标识别、相机坐标 x/y/z。
  不建议在 Jetson 内写死机械臂1/机械臂2/机器人本体外参。

GMK 端：
  负责机器人相关外参：T_robot_camera、T_arm1_robot、T_arm2_robot。
  因为这些外参和 R2 机器人结构、机械臂安装位置、底盘坐标定义强相关。
```

如果相机固定在机器人前方，这更准确地说是**相机到机器人/机械臂基座的外参标定**，不一定是传统“眼在手上”的手眼标定。若机械臂基座固定在车体上，可以通过测量 + 标定板/已知点修正得到这些外参。若相机或机械臂基座会动，则应使用动态 TF/机械臂运动学，而不是固定参数。

---

## 4. 推荐订阅话题

### `/techx/vision/frame`

类型：

```text
techx_vision_bridge/msg/VisionFrame
```

这是推荐给决策包订阅的主话题。一条消息代表 Jetson 的一个完整视觉帧，里面包含本帧全部目标。

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

### `VisionObject` 关键字段

```text
目标语义：zone_id / target_type / class_id / color
检测信息：confidence / u / v
相机坐标：valid_xyz / x / y / z
机器人坐标：valid_robot_xyz / robot_x / robot_y / robot_z
推荐控制坐标：control_frame / valid_control_xyz / control_x / control_y / control_z
对齐误差：align_err_x / align_err_y
排序分数：priority
```

`x/y/z` 永远是 Jetson 原始相机坐标。真正给控制使用时，建议优先看 `control_frame + control_x/y/z`。

`control_frame` 约定：

```text
0 unknown
1 camera_link
2 robot_base
3 arm1_base
4 arm2_base
```

默认映射：

```text
Head -> arm1_base
KFS  -> arm2_base
QR   -> robot_base
```

### 兼容话题

```text
/techx/vision/objects      techx_vision_bridge/msg/VisionObject
/techx/vision/kfs_targets  techx_vision_bridge/msg/VisionTarget
/techx/vision/targets      techx_vision_bridge/msg/Target3D
```

新决策包不建议优先使用兼容话题。`/techx/vision/targets` 不能表达无目标或识别到但无深度。

---

## 5. UDP V2 协议

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
float x;             // camera frame
float y;             // camera frame
float z;             // camera frame, z>0 means valid 3D
```

包长度：

```text
17 + count * 27 + 2
```

最后 2 字节是 CRC16-CCITT，覆盖前面所有字节。

旧 29 字节协议只作兼容，默认不接收：

```yaml
accept_legacy: false
```

比赛主链路应使用 V2。

---

## 6. 移植到其他 GMK 工程

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

## 7. 编译和启动

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

---

## 8. 模拟 Jetson 发送器

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
      if (obj.target_type == techx_vision_bridge::msg::VisionObject::TYPE_WEAPON_HEAD &&
          obj.valid_control_xyz) {
        // 使用 obj.control_x/y/z，坐标系为 arm1_base
      } else if (obj.target_type == techx_vision_bridge::msg::VisionObject::TYPE_KFS &&
                 obj.valid_control_xyz) {
        // 使用 obj.control_x/y/z，坐标系为 arm2_base
      } else if (obj.target_type == techx_vision_bridge::msg::VisionObject::TYPE_QR) {
        // 用 align_err_x/y 做水平对齐；valid_control_xyz 时用 control_z 控制距离
      }
    }
  }
);
```

---

## 10. 决策包处理建议

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
Head 阶段：只看 target_type=TYPE_WEAPON_HEAD，使用 arm1_base control 坐标
KFS 阶段：只看 target_type=TYPE_KFS，使用 arm2_base control 坐标
QR 对齐阶段：只看 target_type=TYPE_QR，用 align_err_x/y
QR 靠近阶段：只看 target_type=TYPE_QR 且 valid_control_xyz=true，用 control_z
```

QR 对齐建议：

```text
abs(align_err_x) < 0.03
abs(align_err_y) < 0.03
```

---

## 11. 常见问题

### 参数没生效

检查 YAML 顶层是不是节点名：

```yaml
vision_bridge_node:
  ros__parameters:
```

### 机械臂坐标明显不对

先确认 Jetson 发来的 `x/y/z` 是否正确，再检查 GMK 的外参：

```yaml
T_robot_camera_xyz_rpy
T_arm1_robot_xyz_rpy
T_arm2_robot_xyz_rpy
```

如果外参还是 0，`control_x/y/z` 只是单位变换后的结果，不能用于实车精确抓取。

### 有 QR 目标但 `/techx/vision/targets` 没数据

正常。旧 XYZ 话题只在 `valid_xyz=true` 时发布。请以 `/techx/vision/frame` 为主。

### 收不到 UDP

```bash
ss -lunp | grep 12345
sudo ufw allow 12345/udp
```

确认 Jetson 的 `target_ip` 是 GMK 网口 IP。

---

## 12. 当前源码文件

仓库只保留必要源码：

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
