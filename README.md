# techx_vision_bridge 使用说明（GMK 视觉功能包）

这份文档只讲 GMK 仓库里的 `techx_vision_bridge` 功能包：**它是什么、怎么启动、需要配置什么、接收什么数据、发布什么数据、其他包怎么给它发请求、怎么拿到对应视觉目标。**

目标读者是第一次接触这个工程的人。看完后应该能做到：

```text
1. 知道这个包在整车里负责什么。
2. 知道启动哪个节点。
3. 知道 Jetson 需要给它发什么 UDP 数据。
4. 知道决策包需要发布什么 request 才能拿到对应目标。
5. 知道 /frame、/request、/selected 里每个关键字段是什么意思。
6. 知道 vision_bridge.yaml 里哪些参数必须改。
```

---

## 0. 最重要结论

### 0.1 这个包现在只有一个运行节点

正常使用只启动一个节点：

```text
vision_bridge_node
```

启动命令：

```bash
ros2 launch techx_vision_bridge vision_bridge.launch.py
```

这个单节点同时完成：

```text
接收 Jetson UDP V2
        ↓
解析目标 class_id / 像素 / camera_link 坐标
        ↓
转换 robot_base / arm1_base / arm2_base 坐标
        ↓
发布完整视觉帧 /techx/vision/frame
        ↓
接收决策包请求 /techx/vision/request
        ↓
发布对应目标 /techx/vision/selected
```

也就是说，小白使用时只需要记住：

```text
启动一个 launch
看 /techx/vision/frame
需要某个目标就发 /techx/vision/request
结果从 /techx/vision/selected 取
```

### 0.2 这个包的两个输入

这个包接收两类输入。

#### 输入 1：Jetson 发来的 UDP V2

Jetson 负责相机、识别、深度，发 UDP 给 GMK。

```text
Jetson UDP V2  -->  vision_bridge_node
```

#### 输入 2：其他 ROS2 包发来的目标请求

比如决策包想要拳头、掌、矛头、KFS、二维码，就往 `/techx/vision/request` 发布请求。

```text
决策包 / 通讯包 / 其他上层包  -->  /techx/vision/request  -->  vision_bridge_node
```

### 0.3 这个包的三个主要输出

| 话题 | 类型 | 作用 |
|---|---|---|
| `/techx/vision/frame` | `VisionFrame` | 完整一帧视觉数据，包含所有目标 |
| `/techx/vision/objects` | `VisionObject` | 单个目标调试流，可选看 |
| `/techx/vision/selected` | `VisionSelection` | 根据 `/request` 筛选出的当前对应目标 |

最常用的是：

```text
完整调试看：/techx/vision/frame
按需求拿目标看：/techx/vision/selected
```

---

## 1. 这个包到底是什么

`techx_vision_bridge` 是 GMK 端的视觉桥接功能包。

它的作用不是控制机器人，而是把 Jetson 的识别结果变成 ROS2 里可直接使用的视觉传感器数据。

它负责：

```text
1. 监听 UDP 端口，接收 Jetson 发来的视觉数据包。
2. 校验 UDP 包是否合法，例如 magic、version、长度、CRC、seq。
3. 读取每个目标的 class_id、颜色、置信度、像素中心、camera_link 坐标。
4. 根据 class_id 判断目标是武器头、KFS 还是二维码。
5. 使用 vision_bridge.yaml 里的外参，把 camera_link 坐标转换到 robot_base、arm1_base、arm2_base。
6. 发布完整视觉帧 /techx/vision/frame。
7. 接收 /techx/vision/request。
8. 根据 request 从最新视觉帧中筛出目标，发布 /techx/vision/selected。
```

它不负责：

```text
不训练模型
不打开相机
不控制底盘电机
不控制机械臂关节
不做导航规划
不做机械臂逆解
不替代决策状态机
```

---

## 2. 最终运行流程

整车中推荐的数据流是：

```text
Jetson + RGB-D 相机
  │
  │  识别武器头 / KFS / 二维码
  │  查询深度
  │  得到 camera_link 坐标
  │  UDP V2 发给 GMK
  ▼
GMK: techx_vision_bridge / vision_bridge_node
  │
  │  接收 UDP
  │  解析目标
  │  转换坐标
  │  发布完整帧 /techx/vision/frame
  │
  │  同时订阅 /techx/vision/request
  │  根据请求发布 /techx/vision/selected
  ▼
决策包
  │
  │  订阅 /techx/vision/frame 或 /techx/vision/selected
  │  决定底盘、机械臂、吸盘、夹爪该怎么动
  ▼
通讯包 / 底盘包 / 机械臂包
  │
  │  执行决策包发来的命令
  ▼
下位机 / 电机 / 执行机构
```

注意：

```text
通讯包一般不直接做视觉决策。
推荐是：决策包读视觉数据，决策包再把动作命令发给通讯包。
```

如果你们暂时没有独立决策包，也可以让通讯包直接订阅 `/selected`，但它仍然必须检查 `status`、`has_match`、`valid_control_xyz`，不能直接拿数据就动作。

---

## 3. “发什么数据，才能得到对应目标”

核心就是 `/techx/vision/request`。

你要对应目标，就给这个包发一个 `VisionRequest`。

例如：

```text
想要拳头武器头：发 class_id=100
想要掌武器头：发 class_id=101
想要矛头武器头：发 class_id=102
想要红方 R2 真 KFS：发 class_id=2
想要蓝方 R2 真 KFS：发 class_id=5
想要二维码：发 class_id=200
```

然后这个包会在 `/techx/vision/selected` 输出当前最新帧里最符合 request 的目标。

重要：

```text
/request 不会命令 Jetson 改模型。
/request 的作用是：在 GMK 已收到的最新视觉帧里筛出你想要的目标。
```

---

## 4. 三个核心话题怎么用

## 4.1 `/techx/vision/frame`，完整视觉帧

类型：

```text
techx_vision_bridge/msg/VisionFrame
```

它表示 Jetson 一帧识别结果。

字段：

| 字段 | 含义 |
|---|---|
| `header` | ROS2 消息头 |
| `seq` | Jetson UDP 帧序号 |
| `protocol_version` | 当前为 2 |
| `upstream_timestamp` | Jetson 时间戳 |
| `target_count` | 本帧目标数量 |
| `has_target` | 本帧是否有目标 |
| `targets[]` | 所有目标，每个都是 `VisionObject` |

`target_count=0` 的含义：

```text
Jetson 和 GMK 通讯在线，但当前这一帧没有目标。
```

长时间没有 `/frame` 消息，才说明链路可能断了。

---

## 4.2 `/techx/vision/request`，目标请求

类型：

```text
techx_vision_bridge/msg/VisionRequest
```

决策包或其他上层包往这里发请求。

字段：

| 字段 | 含义 |
|---|---|
| `request_seq` | 请求序号，自己递增即可 |
| `target_type` | 目标大类，0 表示不限 |
| `zone_id` | 区域，0 表示不限 |
| `use_class_id` | 是否按具体 class_id 筛选 |
| `class_id` | 具体目标编号 |
| `use_color` | 是否按颜色筛选 |
| `color` | 0 未知，1 红，2 蓝 |
| `require_control_xyz` | 是否要求必须有有效三维控制坐标 |
| `min_confidence` | 最低置信度 |
| `max_frame_age_sec` | 允许使用的最大视觉帧年龄，单位秒 |

`target_type`：

| 数值 | 含义 |
|---:|---|
| 0 | 任意 |
| 1 | 武器头 |
| 2 | KFS |
| 3 | 二维码 |
| 10 | 自定义 |

---

## 4.3 `/techx/vision/selected`，请求对应结果

类型：

```text
techx_vision_bridge/msg/VisionSelection
```

它是这个包根据最新 request 给出的结果。

字段：

| 字段 | 含义 |
|---|---|
| `frame_seq` | 结果来自哪一帧视觉数据 |
| `request_seq` | 对应哪个请求 |
| `has_request` | 是否已经收到 request |
| `has_match` | 是否找到符合 request 的目标 |
| `status` | 当前状态 |
| `selected_index` | 选中的目标在 frame.targets[] 中的索引 |
| `frame_age_sec` | 当前视觉帧已经过去多久 |
| `score` | 选择评分 |
| `target` | 选中的目标，类型是 `VisionObject` |

`status`：

| 数值 | 含义 | 能不能用 |
|---:|---|---|
| 0 | 正常找到目标 | 继续检查坐标有效后可用 |
| 1 | 没收到 request | 不能用 |
| 2 | 没收到视觉帧 | 不能用 |
| 3 | 没找到匹配目标 | 不能用 |
| 4 | 视觉帧太旧 | 不能用 |
| 5 | request 太旧 | 不能用 |

使用 `/selected` 控制前必须检查：

```text
status == 0
has_match == true
```

如果要用三维坐标，还必须检查：

```text
target.valid_control_xyz == true
```

---

## 5. VisionObject 每个字段含义

`VisionObject` 是一个目标的数据结构。

### 5.1 目标身份

| 字段 | 含义 |
|---|---|
| `zone_id` | 区域：1 武器头，2 KFS，3 二维码 |
| `target_type` | 目标大类：1 武器头，2 KFS，3 二维码 |
| `class_id` | 最具体的目标编号 |
| `color` | 0 未知，1 红，2 蓝 |
| `confidence` | 置信度 |

### 5.2 像素和对齐

| 字段 | 含义 |
|---|---|
| `u` | 图像中心点横坐标 |
| `v` | 图像中心点纵坐标 |
| `align_err_x` | 相对图像中心的横向误差，可用于底盘水平对齐 |
| `align_err_y` | 相对图像中心的纵向误差 |

### 5.3 相机坐标

| 字段 | 含义 |
|---|---|
| `valid_xyz` | camera_link 坐标是否有效 |
| `x/y/z` | 目标在 camera_link 下的位置，单位 m |

这是 Jetson 原始输出。

### 5.4 机器人本体坐标

| 字段 | 含义 |
|---|---|
| `valid_robot_xyz` | robot_base 坐标是否有效 |
| `robot_x/y/z` | 目标在机器人本体坐标系下的位置，单位 m |

底盘靠近武器头、靠近 KFS、二维码对齐和靠近，都优先用这个。

### 5.5 机械臂1坐标

| 字段 | 含义 |
|---|---|
| `valid_arm1_xyz` | arm1_base 坐标是否有效 |
| `arm1_x/y/z` | 目标在机械臂1基座坐标系下的位置，单位 m |

机械臂1抓武器头用这个。

### 5.6 机械臂2坐标

| 字段 | 含义 |
|---|---|
| `valid_arm2_xyz` | arm2_base 坐标是否有效 |
| `arm2_x/y/z` | 目标在机械臂2基座坐标系下的位置，单位 m |

机械臂2操作 KFS 用这个。

### 5.7 推荐控制坐标

| 字段 | 含义 |
|---|---|
| `control_frame` | 推荐坐标系 |
| `valid_control_xyz` | 推荐坐标是否有效 |
| `control_x/y/z` | 推荐控制坐标 |

`control_frame`：

| 数值 | 坐标系 |
|---:|---|
| 1 | camera_link |
| 2 | robot_base |
| 3 | arm1_base |
| 4 | arm2_base |

注意：`control_x/y/z` 是默认推荐值，不是唯一能用的值。

更稳的规则：

```text
底盘用 robot_x/y/z
机械臂1用 arm1_x/y/z
机械臂2用 arm2_x/y/z
```

---

## 6. 比赛 class_id 分配

UDP V2 里不传字符串，只传 `class_id`。

### 6.1 武器头

| class_id | 中文 | target_type | zone_id | 推荐坐标 |
|---:|---|---:|---:|---|
| 100 | 拳头 | 1 | 1 | arm1_base |
| 101 | 掌 | 1 | 1 | arm1_base |
| 102 | 矛头 | 1 | 1 | arm1_base |

使用：

```text
底盘靠近：robot_x/y/z
机械臂1抓取：arm1_x/y/z
```

### 6.2 KFS

| class_id | 中文 | color | target_type | zone_id | 推荐坐标 |
|---:|---|---:|---:|---:|---|
| 0 | 红方 R1 KFS | 1 | 2 | 2 | arm2_base |
| 1 | 红方 R2 假 KFS | 1 | 2 | 2 | arm2_base |
| 2 | 红方 R2 真 KFS | 1 | 2 | 2 | arm2_base |
| 3 | 蓝方 R1 KFS | 2 | 2 | 2 | arm2_base |
| 4 | 蓝方 R2 假 KFS | 2 | 2 | 2 | arm2_base |
| 5 | 蓝方 R2 真 KFS | 2 | 2 | 2 | arm2_base |

梅花林阶段一般要精确请求：

```text
红方 R2 真 KFS：class_id = 2
蓝方 R2 真 KFS：class_id = 5
```

### 6.3 二维码

| class_id | 中文 | target_type | zone_id | 推荐坐标 |
|---:|---|---:|---:|---|
| 200 | 二维码 | 3 | 3 | robot_base |

二维码对齐：

```text
align_err_x / align_err_y
```

二维码靠近：

```text
robot_x/y/z 或 control_z
```

当前协议不传二维码字符串内容。

---

## 7. 常用请求例子

### 7.1 请求拳头武器头

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

结果看：

```bash
ros2 topic echo /techx/vision/selected
```

控制使用：

```text
底盘：selected.target.robot_x/y/z
机械臂1：selected.target.arm1_x/y/z
```

### 7.2 请求掌

```bash
ros2 topic pub --once /techx/vision/request techx_vision_bridge/msg/VisionRequest "{
  request_seq: 2,
  target_type: 1,
  zone_id: 1,
  use_class_id: true,
  class_id: 101,
  use_color: false,
  require_control_xyz: true,
  min_confidence: 0.4,
  max_frame_age_sec: 0.2
}"
```

### 7.3 请求矛头

```bash
ros2 topic pub --once /techx/vision/request techx_vision_bridge/msg/VisionRequest "{
  request_seq: 3,
  target_type: 1,
  zone_id: 1,
  use_class_id: true,
  class_id: 102,
  use_color: false,
  require_control_xyz: true,
  min_confidence: 0.4,
  max_frame_age_sec: 0.2
}"
```

### 7.4 请求红方 R2 真 KFS

```bash
ros2 topic pub --once /techx/vision/request techx_vision_bridge/msg/VisionRequest "{
  request_seq: 4,
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

### 7.5 请求蓝方 R2 真 KFS

```bash
ros2 topic pub --once /techx/vision/request techx_vision_bridge/msg/VisionRequest "{
  request_seq: 5,
  target_type: 2,
  zone_id: 2,
  use_class_id: true,
  class_id: 5,
  use_color: false,
  require_control_xyz: true,
  min_confidence: 0.4,
  max_frame_age_sec: 0.2
}"
```

### 7.6 请求二维码

二维码水平对齐时可以不强制三维坐标：

```bash
ros2 topic pub --once /techx/vision/request techx_vision_bridge/msg/VisionRequest "{
  request_seq: 6,
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

如果二维码靠近需要距离，就要确认：

```text
selected.target.valid_robot_xyz == true
```

---

## 8. Jetson UDP V2 数据包

Jetson 每次推理完成会发一个 UDP V2 包。

结构：

```text
Header 17 字节
Target 0~16 个，每个 27 字节
CRC16 2 字节
```

Header：

| 字段 | 类型 | 含义 |
|---|---|---|
| `magic` | uint16 | 固定 0x55AB |
| `version` | uint8 | 当前为 2 |
| `flags` | uint8 | 预留 |
| `seq` | uint32 | 帧序号 |
| `timestamp` | double | Jetson 时间戳 |
| `count` | uint8 | 目标数量，0~16 |

Target：

| 字段 | 类型 | 含义 |
|---|---|---|
| `track_id` | uint8 | 跟踪 ID |
| `class_id` | uint8 | 全局类别编号 |
| `color` | uint8 | 0 未知，1 红，2 蓝 |
| `confidence` | float32 | 置信度 |
| `u/v` | float32 | 像素中心 |
| `x/y/z` | float32 | camera_link 坐标，单位 m |

`count=0` 表示：

```text
通信在线，但当前帧没有目标。
```

`z=0` 通常表示：

```text
识别到了目标，但是深度无效，不能用于三维控制。
```

---

## 9. 坐标系和外参

这个包内部负责统一转换：

```text
p_robot = T_robot_camera * p_camera
p_arm1  = T_arm1_robot  * p_robot
p_arm2  = T_arm2_robot  * p_robot
```

配置在：

```text
src/techx_vision_bridge/config/vision_bridge.yaml
```

参数：

```yaml
T_robot_camera_xyz_rpy: [x, y, z, roll, pitch, yaw]
T_arm1_robot_xyz_rpy: [x, y, z, roll, pitch, yaw]
T_arm2_robot_xyz_rpy: [x, y, z, roll, pitch, yaw]
```

单位：

```text
x/y/z：米
roll/pitch/yaw：弧度
```

其他包不要再重复做 `camera_link -> robot_base / arm_base`。这个包已经输出了转换后的坐标。

---

## 10. vision_bridge.yaml 需要填什么

最重要的是这些：

```yaml
udp_port: 12345
frame_topic_name: "/techx/vision/frame"
request_topic_name: "/techx/vision/request"
selected_topic_name: "/techx/vision/selected"
enable_request_selector: true
image_width: 640.0
image_height: 480.0
class_rules:
  - "0-5:2:2:4:0.0"
  - "100-102:1:1:3:0.0"
  - "200:3:3:2:0.0"
T_robot_camera_xyz_rpy: [...]
T_arm1_robot_xyz_rpy: [...]
T_arm2_robot_xyz_rpy: [...]
```

`class_rules` 格式：

```text
"class_or_range:zone_id:target_type:control_frame:priority_bias"
```

`control_frame`：

```text
2 = robot_base
3 = arm1_base
4 = arm2_base
```

默认含义：

```text
0~5 KFS -> 默认 control 是 arm2_base
100~102 武器头 -> 默认 control 是 arm1_base
200 二维码 -> 默认 control 是 robot_base
```

---

## 11. 没有硬件怎么测试

### 11.1 编译

```bash
cd ~/gmk_ws
rm -rf build install log
colcon build --packages-select techx_vision_bridge
source install/setup.bash
```

### 11.2 检查配置

```bash
ros2 run techx_vision_bridge check_vision_bridge_config.py \
  --config src/techx_vision_bridge/config/vision_bridge.yaml
```

### 11.3 启动一个节点

```bash
ros2 launch techx_vision_bridge vision_bridge.launch.py
```

### 11.4 模拟 Jetson 发数据

```bash
ros2 run techx_vision_bridge mock_jetson_sender.py --mode mixed --ip 127.0.0.1
```

### 11.5 看完整视觉帧

```bash
ros2 topic echo /techx/vision/frame
```

### 11.6 发请求并看对应结果

发请求：

```bash
ros2 topic pub --once /techx/vision/request techx_vision_bridge/msg/VisionRequest "{
  request_seq: 1,
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

看结果：

```bash
ros2 topic echo /techx/vision/selected
```

如果正常，会看到：

```text
status: 0
has_match: true
target.class_id: 200
```

---

## 12. 外参标定结果怎么用

标定可以在 Jetson 端采点，也可以用标定板、机械臂辅助采点。

但是结果最后填这里：

```yaml
T_robot_camera_xyz_rpy: [...]
T_arm1_robot_xyz_rpy: [...]
T_arm2_robot_xyz_rpy: [...]
```

这个包只负责运行时使用这些外参。

仓库提供工具：

```bash
ros2 run techx_vision_bridge estimate_extrinsic_from_points.py \
  --csv robot_camera_points.csv \
  --name T_robot_camera
```

CSV：

```csv
from_x,from_y,from_z,to_x,to_y,to_z
0.10,0.02,0.80,0.80,-0.10,0.25
```

对于 `T_robot_camera`：

```text
from = camera_link 点
to = robot_base 点
```

工具输出的 YAML 数组手动复制到 `vision_bridge.yaml`。

---

## 13. 实车使用安全检查

拿 `/selected` 控制前必须检查：

```text
status == 0
has_match == true
frame_age_sec < 0.2
target.confidence 足够高
```

如果用三维控制，还要检查：

```text
target.valid_control_xyz == true
```

建议决策包再做：

```text
连续 3~5 帧稳定
坐标不能突然跳变
视觉断流立即停止视觉闭环动作
```

---

## 14. 小白最短使用流程

```text
1. colcon build 编译。
2. 跑 check_vision_bridge_config.py 检查配置。
3. ros2 launch 启动 vision_bridge_node。
4. 用 mock_jetson_sender.py 模拟 Jetson。
5. echo /techx/vision/frame 确认有完整帧。
6. 发布 /techx/vision/request 请求目标。
7. echo /techx/vision/selected 看对应结果。
8. 接真实 Jetson。
9. 填真实外参。
10. 决策包根据 /selected 或 /frame 生成控制命令。
```

---

## 15. 最容易用错的地方

```text
1. 现在只有一个运行节点 vision_bridge_node，不要再找 vision_selector_node。
2. /request 是筛选请求，不是让 Jetson 改识别模型。
3. /selected 是 request 对应结果，/frame 是完整事实。
4. 底盘用 robot_x/y/z。
5. 机械臂1用 arm1_x/y/z。
6. 机械臂2用 arm2_x/y/z。
7. control_x/y/z 要先看 control_frame。
8. 没有有效 z 时不要做三维控制。
9. 外参最终填 GMK 的 vision_bridge.yaml。
10. 通讯包一般只执行决策包命令，不建议直接替代决策包。
```
