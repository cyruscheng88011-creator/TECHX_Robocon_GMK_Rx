# techx_vision_bridge 小白使用说明

`techx_vision_bridge` 是 **GMK 端 ROS 2 视觉桥接包**。它的作用可以理解成：

```text
把 Jetson 视觉识别结果，变成 GMK/决策/底盘/机械臂都能直接订阅的 ROS 2 传感器数据。
```

它不负责训练模型，不负责相机驱动，不负责导航规划，不负责机械臂运动规划，也不直接控制电机。它负责的事情很明确：

```text
1. 接收 Jetson 发来的 UDP V2 视觉数据包。
2. 检查数据包是否合法，例如长度、版本、CRC、序号。
3. 根据 class_id 判断目标是什么。
4. 把 Jetson 的 camera_link 坐标转换成 robot_base、arm1_base、arm2_base。
5. 发布 ROS 2 话题给决策包、导航包、机械臂包使用。
6. 可选地根据决策包请求，从完整视觉帧里筛出当前最优目标。
```

---

## 0. 先看结论

### 这个包是不是 Jetson 端？

不是。

```text
Jetson 端：识别目标，算相机坐标，发 UDP。
GMK 端：收 UDP，做坐标变换，发布 ROS 2 话题。
```

### 如果要标定，是不是在 Jetson 端操作？

**不完全是。**

Jetson 参与标定数据采集，因为它能给出目标在 `camera_link` 下的坐标；但是最终标定结果要写进 GMK 的：

```text
src/techx_vision_bridge/config/vision_bridge.yaml
```

也就是：

```text
Jetson 负责提供相机坐标点。
你根据实物测量或多点拟合求外参。
GMK 负责使用外参做坐标变换。
```

### 其他包还要不要自己转换相机坐标？

不要。

其他包不要再自己做：

```text
camera_link -> robot_base
camera_link -> arm1_base
camera_link -> arm2_base
```

这些转换统一放在 `techx_vision_bridge` 内部。其他包直接读取已经转换好的字段。

### 底盘也要用武器头/KFS 数据怎么办？

每个目标都会同时输出多套坐标：

```text
robot_x/y/z   给底盘或导航用
arm1_x/y/z    给机械臂1用
arm2_x/y/z    给机械臂2用
control_x/y/z 默认推荐坐标
```

所以武器头和 KFS 即使主要给机械臂操作，也可以同时给底盘调整位置。

---

## 1. 整体数据流

完整运行链路是：

```text
Jetson + 相机
  │
  │ 1. 识别武器头、KFS、二维码
  │ 2. 查询深度
  │ 3. 输出 camera_link 坐标
  │ 4. UDP V2 发给 GMK
  ▼
GMK: vision_bridge_node
  │
  │ 1. 收 UDP
  │ 2. 校验 CRC/seq
  │ 3. class_id 映射目标类型
  │ 4. camera_link -> robot_base / arm1_base / arm2_base
  │ 5. 发布 /techx/vision/frame
  ▼
GMK: vision_selector_node，可选
  │
  │ 1. 接收决策包 /techx/vision/request
  │ 2. 从最新 /techx/vision/frame 里筛目标
  │ 3. 发布 /techx/vision/selected
  ▼
决策包 / 导航包 / 机械臂包
  │
  │ 读取 ROS 2 话题，不解析 UDP，不重新做相机外参变换
```

---

## 2. 两个节点分别做什么

### 2.1 `vision_bridge_node`

这是主节点，必须运行。

它负责：

```text
UDP 接收
协议校验
目标语义映射
坐标转换
发布完整视觉帧
```

主输出：

```text
/techx/vision/frame
```

### 2.2 `vision_selector_node`

这是辅助节点，推荐运行。

它负责：

```text
接收 /techx/vision/request
从最新 /techx/vision/frame 里筛出当前最优目标
发布 /techx/vision/selected
```

注意：`vision_selector_node` **不会命令 Jetson 只识别某个目标**。Jetson 仍然持续识别当前启用的所有目标。selector 只是从最新一帧结果里帮决策包筛选。

---

## 3. 三个最重要的话题

### 3.1 `/techx/vision/frame`

这是主数据源。任何调试和复杂决策都应该先看它。

类型：

```text
techx_vision_bridge/msg/VisionFrame
```

它表示 Jetson 的一帧完整视觉结果，里面有所有目标：

```text
seq
protocol_version
upstream_timestamp
target_count
has_target
targets[]
```

每个 `targets[]` 里是一个 `VisionObject`。

### 3.2 `/techx/vision/request`

这是决策包发给 selector 的请求。

类型：

```text
techx_vision_bridge/msg/VisionRequest
```

含义是：

```text
我当前任务阶段只关心某种目标，请帮我从最新 frame 里筛出来。
```

### 3.3 `/techx/vision/selected`

这是 selector 输出的当前最优目标。

类型：

```text
techx_vision_bridge/msg/VisionSelection
```

控制前必须检查：

```text
status == STATUS_OK
has_match == true
```

如果要用三维坐标控制，还必须检查：

```text
target.valid_control_xyz == true
```

---

## 4. 比赛目标 class_id 分配

`class_id` 是通信协议里的全局编号。它不是某个 YOLO 模型内部的类别顺序。

Jetson 必须把模型输出映射成下面这张表。GMK、决策包、机械臂包都只认这张表。

### 4.1 武器头，机械臂1操作

| class_id | 名称 | 中文 | 推荐主操作坐标系 |
|---:|---|---|---|
| 100 | `weapon_head_fist` | 拳头 | `arm1_base` |
| 101 | `weapon_head_palm` | 掌 | `arm1_base` |
| 102 | `weapon_head_spear` | 矛头 | `arm1_base` |

注意：底盘也可能需要根据武器头移动，所以不要只看 `control_x/y/z`，底盘要看 `robot_x/y/z`。

### 4.2 KFS，机械臂2操作

| class_id | 名称 | 中文 | color | 推荐主操作坐标系 |
|---:|---|---|---:|---|
| 0 | `kfs_red_r1` | 红方 R1 KFS | 1 | `arm2_base` |
| 1 | `kfs_red_r2_fake` | 红方 R2 假 KFS | 1 | `arm2_base` |
| 2 | `kfs_red_r2_true` | 红方 R2 真 KFS | 1 | `arm2_base` |
| 3 | `kfs_blue_r1` | 蓝方 R1 KFS | 2 | `arm2_base` |
| 4 | `kfs_blue_r2_fake` | 蓝方 R2 假 KFS | 2 | `arm2_base` |
| 5 | `kfs_blue_r2_true` | 蓝方 R2 真 KFS | 2 | `arm2_base` |

梅花林阶段一般不要只筛 `target_type=KFS`，而要精确筛：

```text
红方 R2 真 KFS：class_id = 2
蓝方 R2 真 KFS：class_id = 5
```

### 4.3 二维码，机器人本体操作

| class_id | 名称 | 中文 | 推荐主操作坐标系 |
|---:|---|---|---|
| 200 | `qr_code` | 二维码 | `robot_base` |

当前 V2 协议只传二维码中心和坐标，不传二维码字符串内容。如果以后需要读取二维码内容，要扩展协议。

---

## 5. 坐标系到底是什么

一共有四个固定坐标系。

### 5.1 `camera_link`

相机坐标系。Jetson 发来的 `x/y/z` 永远属于它。

一般 RGB-D 相机常见含义是：

```text
X：图像右方
Y：图像下方
Z：相机前方
```

具体仍以 Jetson 相机解算代码为准。

### 5.2 `robot_base`

机器人本体坐标系。建议提前在机械设计和控制里固定：

```text
X：机器人前方
Y：机器人左方
Z：机器人上方
```

底盘控制、二维码对齐、目标相对机器人位置都应该用它。

### 5.3 `arm1_base`

机械臂1基座坐标系。武器头抓取、武器头对接时，机械臂1应该用它。

### 5.4 `arm2_base`

机械臂2基座坐标系。KFS 操作时，机械臂2应该用它。

---

## 6. 外参方向，千万不要填反

本包采用 `T_to_from` 命名方式。

意思是：

```text
T_robot_camera：把 camera_link 点转换到 robot_base
T_arm1_robot： 把 robot_base 点转换到 arm1_base
T_arm2_robot： 把 robot_base 点转换到 arm2_base
```

公式：

```text
p_robot = T_robot_camera * p_camera
p_arm1  = T_arm1_robot  * p_robot
p_arm2  = T_arm2_robot  * p_robot
```

YAML 里写：

```yaml
T_robot_camera_xyz_rpy: [x, y, z, roll, pitch, yaw]
T_arm1_robot_xyz_rpy:  [x, y, z, roll, pitch, yaw]
T_arm2_robot_xyz_rpy:  [x, y, z, roll, pitch, yaw]
```

单位：

```text
x/y/z：米
roll/pitch/yaw：弧度
```

旋转顺序：

```text
R = Rz(yaw) * Ry(pitch) * Rx(roll)
p_out = R * p_in + t
```

如果你手上测到的是反方向，比如 `T_camera_robot`，不能直接填。必须先求逆。

---

## 7. `VisionObject` 里每个字段怎么用

一个 `VisionObject` 同时包含多套坐标。

### 7.1 目标语义

```text
zone_id       目标区域
目标类型 target_type
class_id      精确类别编号
color         0 unknown, 1 red, 2 blue
confidence    置信度
```

### 7.2 像素信息

```text
u / v          目标中心像素坐标
align_err_x    相对画面中心的水平误差
align_err_y    相对画面中心的垂直误差
```

QR 对齐时常用 `align_err_x/y`。

### 7.3 相机坐标

```text
valid_xyz
x / y / z
```

这是 Jetson 原始 `camera_link` 坐标。普通下游包不建议直接用它控制。

### 7.4 机器人本体坐标

```text
valid_robot_xyz
robot_x / robot_y / robot_z
```

底盘移动、目标靠近、QR 对齐靠近时使用它。

### 7.5 机械臂1坐标

```text
valid_arm1_xyz
arm1_x / arm1_y / arm1_z
```

武器头抓取时，机械臂1使用它。

### 7.6 机械臂2坐标

```text
valid_arm2_xyz
arm2_x / arm2_y / arm2_z
```

KFS 操作时，机械臂2使用它。

### 7.7 推荐坐标

```text
control_frame
valid_control_xyz
control_x / control_y / control_z
```

默认规则：

```text
武器头：control = arm1_base
KFS：   control = arm2_base
QR：    control = robot_base
```

但是武器头/KFS 也要控制底盘时，底盘仍然应该看 `robot_x/y/z`。

---

## 8. 决策包应该怎么用

### 8.1 Head 阶段

如果要抓拳头：

```text
class_id = 100
```

如果要抓掌：

```text
class_id = 101
```

如果要抓矛头：

```text
class_id = 102
```

底盘先用：

```text
robot_x / robot_y / robot_z
```

机械臂1再用：

```text
arm1_x / arm1_y / arm1_z
```

### 8.2 KFS 阶段

红方 R2 真：

```text
class_id = 2
```

蓝方 R2 真：

```text
class_id = 5
```

底盘先用：

```text
robot_x / robot_y / robot_z
```

机械臂2再用：

```text
arm2_x / arm2_y / arm2_z
```

### 8.3 QR 阶段

二维码：

```text
class_id = 200
```

水平对齐：

```text
align_err_x / align_err_y
```

靠近：

```text
robot_z 或 control_z
```

前提：

```text
valid_robot_xyz == true
```

---

## 9. request / selected 怎么用

`/techx/vision/frame` 是主数据源。`request/selected` 只是为了让决策包更方便地拿当前阶段最优目标。

### 9.1 请求拳头

```bash
ros2 topic pub --once /techx/vision/request techx_vision_bridge/msg/VisionRequest "{
  request_seq: 1,
  target_type: 1,
  zone_id: 1,
  use_class_id: true,
  class_id: 100,
  use_color: false,
  require_control_xyz: true,
  min_confidence: 0.4,
  max_frame_age_sec: 0.2
}"
```

### 9.2 请求红方 R2 真 KFS

```bash
ros2 topic pub --once /techx/vision/request techx_vision_bridge/msg/VisionRequest "{
  request_seq: 2,
  target_type: 2,
  zone_id: 2,
  use_class_id: true,
  class_id: 2,
  use_color: false,
  require_control_xyz: true,
  min_confidence: 0.4,
  max_frame_age_sec: 0.2
}"
```

### 9.3 请求二维码

```bash
ros2 topic pub --once /techx/vision/request techx_vision_bridge/msg/VisionRequest "{
  request_seq: 3,
  target_type: 3,
  zone_id: 3,
  use_class_id: true,
  class_id: 200,
  use_color: false,
  require_control_xyz: false,
  min_confidence: 0.3,
  max_frame_age_sec: 0.2
}"
```

查看结果：

```bash
ros2 topic echo /techx/vision/selected
```

控制前检查：

```text
status == 0
has_match == true
```

如果要用 3D 坐标：

```text
target.valid_control_xyz == true
```

---

## 10. 编译和启动

把源码放到工作空间：

```text
~/gmk_ws/src/techx_vision_bridge
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

查看话题：

```bash
ros2 topic list | grep techx
```

应看到类似：

```text
/techx/vision/frame
/techx/vision/objects
/techx/vision/request
/techx/vision/selected
```

---

## 11. 没有硬件怎么测试

没有 Jetson、没有相机，也可以先测试 GMK 软件链路。

终端 1，启动 GMK bridge：

```bash
source /opt/ros/humble/setup.bash
cd ~/gmk_ws
source install/setup.bash
ros2 launch techx_vision_bridge vision_bridge.launch.py
```

终端 2，看完整帧：

```bash
source ~/gmk_ws/install/setup.bash
ros2 topic echo /techx/vision/frame
```

终端 3，模拟 Jetson：

```bash
source ~/gmk_ws/install/setup.bash
ros2 run techx_vision_bridge mock_jetson_sender.py --mode mixed --ip 127.0.0.1
```

如果看到 `/techx/vision/frame` 有目标，说明：

```text
UDP 接收 OK
协议解析 OK
ROS 2 发布 OK
消息类型 OK
```

再测试 selector：

```bash
ros2 topic pub --once /techx/vision/request techx_vision_bridge/msg/VisionRequest "{
  request_seq: 1,
  target_type: 2,
  zone_id: 2,
  use_class_id: false,
  use_color: false,
  require_control_xyz: true,
  min_confidence: 0.3,
  max_frame_age_sec: 0.2
}"

ros2 topic echo /techx/vision/selected
```

---

## 12. 配置检查，不需要硬件

在 GMK 仓库根目录运行：

```bash
python3 src/techx_vision_bridge/tools/check_vision_bridge_config.py \
  --config src/techx_vision_bridge/config/vision_bridge.yaml
```

正常应该输出：

```text
OK: GMK vision bridge config matches the competition communication contract
```

这个脚本会检查：

```text
YAML 顶层是否有 vision_bridge_node / vision_selector_node
class_rules 是否覆盖 0~5、100~102、200
旧兼容话题是否默认关闭
外参字段是否存在
```

---

## 13. 手眼/外参标定到底怎么做

### 13.1 先明确：这里不是典型 eye-in-hand

你的相机固定在机器人前方，不在机械臂末端。

所以更准确叫：

```text
固定相机到机器人本体的外参标定
机器人本体到机械臂基座的外参标定
```

需要得到三个变换：

```text
T_robot_camera
T_arm1_robot
T_arm2_robot
```

### 13.2 `T_robot_camera` 怎么标

目标是求：

```text
p_robot = T_robot_camera * p_camera
```

操作流程：

```text
1. 固定相机，固定机器人坐标系 robot_base。
2. 在机器人前方放一个明显目标点，例如标定板中心、圆点、AprilTag 中心。
3. 记录 Jetson 给出的 p_camera = [x,y,z]。
4. 同时测量这个点在 robot_base 下的真实位置 p_robot = [x,y,z]。
5. 换多个位置重复采集。
6. 用 estimate_extrinsic_from_points.py 求 T_robot_camera。
```

建议采集：

```text
至少 4 个点
推荐 10~20 个点
点位要覆盖左/右、近/远、高/低
不要全部在一条线上
```

CSV 格式：

```csv
from_x,from_y,from_z,to_x,to_y,to_z
0.10,0.02,0.80,0.80,-0.10,0.25
0.05,0.10,1.00,1.00,-0.05,0.35
```

这里：

```text
from = camera_link 点
  from_x/from_y/from_z 来自 Jetson

to = robot_base 点
  to_x/to_y/to_z 来自实物测量
```

运行：

```bash
python3 src/techx_vision_bridge/tools/estimate_extrinsic_from_points.py \
  --csv robot_camera_points.csv \
  --name T_robot_camera
```

输出会包含：

```text
T_robot_camera_xyz_rpy: [x, y, z, roll, pitch, yaw]
```

把这一行后面的数组复制到 `vision_bridge.yaml`。

### 13.3 `T_arm1_robot` 怎么标

目标是求：

```text
p_arm1 = T_arm1_robot * p_robot
```

最简单方法是机械测量或 CAD：

```text
1. 定义 robot_base 原点和 XYZ 方向。
2. 定义 arm1_base 原点和 XYZ 方向。
3. 测量 arm1_base 原点相对 robot_base 的位置。
4. 测量 arm1_base 坐标轴相对 robot_base 的旋转。
5. 写成 [x,y,z,roll,pitch,yaw]。
```

如果机械结构复杂，也可以用点对拟合：

```text
from = robot_base 下的点
to = arm1_base 下的同一个点
```

运行：

```bash
python3 src/techx_vision_bridge/tools/estimate_extrinsic_from_points.py \
  --csv arm1_robot_points.csv \
  --name T_arm1_robot
```

### 13.4 `T_arm2_robot` 怎么标

同理：

```text
p_arm2 = T_arm2_robot * p_robot
```

运行：

```bash
python3 src/techx_vision_bridge/tools/estimate_extrinsic_from_points.py \
  --csv arm2_robot_points.csv \
  --name T_arm2_robot
```

### 13.5 标定结果好不好怎么看

脚本会输出：

```text
rmse_m
max_error_m
```

经验判断：

```text
RMSE < 0.01 m：很好
RMSE 0.01~0.03 m：一般可用
RMSE > 0.03 m：要检查点位、单位、坐标轴、深度误差
```

### 13.6 标定后必须验证

不要标完就直接高速控制。

验证步骤：

```text
1. 放一个目标在已知 robot_base 坐标位置。
2. 看 /techx/vision/frame 里的 robot_x/y/z 是否接近真实值。
3. 把目标放到机械臂1可达区域，看 arm1_x/y/z 是否合理。
4. 把目标放到机械臂2可达区域，看 arm2_x/y/z 是否合理。
5. 低速让底盘或机械臂靠近，不要一开始就闭环抓取。
```

---

## 14. 新增识别目标怎么做

假设以后新增一个对接 marker，分配 `class_id=150~159`。

### Jetson 端

在 Jetson `config.json` 里新增模型，并保证输出全局 class_id。

### GMK 端

在 `vision_bridge.yaml` 加：

```yaml
class_rules:
  - "0-5:2:2:4:0.0"
  - "100-102:1:1:3:0.0"
  - "200:3:3:2:0.0"
  - "150-159:10:10:2:0.0"
```

含义：

```text
150~159 映射成自定义目标，推荐 robot_base 坐标。
```

### 决策包

筛选：

```text
target_type = 10
或 use_class_id=true, class_id=150
```

---

## 15. 软件是不是太复杂

这个工程看起来复杂，是因为它把比赛中必须处理的风险都拆开了：

```text
UDP 协议
目标类别
多目标
无目标
无深度
坐标变换
底盘坐标
机械臂坐标
决策筛选
扩展新目标
旧协议兼容
```

如果把这些全部塞进一个简单脚本，前期看起来简单，后期一定会出现：

```text
坐标到底谁算的说不清
模型类别换了全系统跟着错
底盘和机械臂用的不是同一个目标点
无深度时误动作
调试看不到完整目标列表
```

所以当前结构是为了让小白使用时只记住下面几件事。

### 小白只需要记住 6 步

```text
1. 不改代码，先编译。
2. 运行 check_vision_bridge_config.py 检查 YAML。
3. 没硬件时先用 mock_jetson_sender.py 测 /frame。
4. 决策包先订阅 /frame，看懂数据。
5. 简单阶段控制再用 /request + /selected。
6. 实车前必须填 T_robot_camera / T_arm1_robot / T_arm2_robot。
```

---

## 16. 常见问题

### Q1：为什么我看到目标，但没有三维坐标？

可能 Jetson 没有有效深度。检查：

```text
valid_xyz
valid_robot_xyz
valid_control_xyz
```

如果这些是 false，不要用三维坐标控制。

### Q2：为什么 KFS 有很多个？

KFS 分六种。梅花林阶段一般要精确筛：

```text
红方 R2 真：class_id=2
蓝方 R2 真：class_id=5
```

### Q3：为什么武器头的 control_frame 是 arm1_base，但底盘也要动？

`control_frame` 是默认推荐坐标。底盘要看同一个目标里的：

```text
robot_x / robot_y / robot_z
```

机械臂1看：

```text
arm1_x / arm1_y / arm1_z
```

### Q4：参数改了没生效怎么办？

确认 YAML 顶层是节点名：

```yaml
vision_bridge_node:
  ros__parameters:

vision_selector_node:
  ros__parameters:
```

改过 `.msg` 后必须 clean build：

```bash
rm -rf build install log
colcon build --packages-select techx_vision_bridge
source install/setup.bash
```

### Q5：收不到 UDP 怎么办？

GMK 上检查端口：

```bash
ss -lunp | grep 12345
```

防火墙允许 UDP：

```bash
sudo ufw allow 12345/udp
```

确认 Jetson 的 `target_ip` 是 GMK 网口 IP。

### Q6：是否需要使用旧 `/techx/vision/targets`？

不建议。

主接口是：

```text
/techx/vision/frame
```

辅助接口是：

```text
/techx/vision/request
/techx/vision/selected
```

旧话题默认关闭，避免误用不完整数据。

---

## 17. 最终使用建议

最推荐的使用方式：

```text
调试和复杂决策：
  订阅 /techx/vision/frame

简单阶段目标获取：
  发布 /techx/vision/request
  订阅 /techx/vision/selected

底盘：
  使用 robot_x/y/z

机械臂1：
  使用 arm1_x/y/z

机械臂2：
  使用 arm2_x/y/z

不要：
  在其他包重复做 camera_link 到 robot/arm 的外参转换
```

实车前必须完成：

```text
1. Jetson 模型 class_id 映射检查
2. GMK YAML 检查
3. mock Jetson 软件闭环
4. T_robot_camera 外参标定
5. T_arm1_robot / T_arm2_robot 外参标定
6. 低速实车验证
```
