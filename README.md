# techx_vision_bridge 使用说明（GMK 视觉功能包）

这份文档只讲 GMK 仓库里的 `techx_vision_bridge` 功能包：它是什么、怎么启动、接收什么、发布什么、其他包应该给它发什么 request、每个字段是什么意思、需要配置哪些参数、断联时会发生什么。

目标读者是第一次接触这个工程的人。只要按本文走，就能知道这个包如何和 Jetson、决策包、通讯包、底盘包、机械臂包配合。

---

## 0. 当前最终结论

### 0.1 这个包现在只有一个运行节点

正常运行只启动一个节点：

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
校验 magic / version / length / CRC / seq
        ↓
解析 class_id / color / confidence / u/v / camera_link x/y/z
        ↓
根据 class_rules 判断目标语义
        ↓
把 camera_link 转成 robot_base / arm1_base / arm2_base
        ↓
发布完整视觉帧 /techx/vision/frame
        ↓
接收上层请求 /techx/vision/request
        ↓
发布对应目标 /techx/vision/selected
```

小白只需要记住：

```text
启动一个 launch。
完整调试看 /techx/vision/frame。
想要某个目标就发 /techx/vision/request。
对应结果从 /techx/vision/selected 取。
```

### 0.2 这个包不是控制包

它不直接控制电机，也不直接规划机械臂。推荐分工：

```text
Jetson：相机、识别、深度、camera_link 坐标、UDP V2 发送。
GMK techx_vision_bridge：UDP 接收、坐标转换、ROS2 视觉话题发布、按 request 筛目标。
决策包：根据视觉数据决定任务动作。
通讯包/底盘包/机械臂包：执行决策包给出的命令。
```

通讯包可以临时订阅视觉话题做测试，但正式结构里不建议通讯包直接做视觉决策。

---

## 1. 整体数据流

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
  │  校验数据包
  │  class_id -> target_type / zone_id / control_frame
  │  camera_link -> robot_base / arm1_base / arm2_base
  │  发布 /techx/vision/frame
  │
  │  同时订阅 /techx/vision/request
  │  根据 request 从最新 frame 里筛目标
  │  发布 /techx/vision/selected
  ▼
决策包
  │
  │  读取 /techx/vision/frame 或 /techx/vision/selected
  │  判断当前任务阶段
  │  输出底盘、机械臂、吸盘、夹爪动作命令
  ▼
通讯包 / 底盘包 / 机械臂包
  │
  │  把动作命令发给下位机或执行机构
  ▼
电机 / 机械臂 / 底盘 / 吸盘 / 夹爪
```

注意：`/techx/vision/request` 不是让 Jetson 只识别某个目标。Jetson 仍持续识别当前启用的所有目标；request 只是让 GMK 在最新视觉帧中筛出你当前想要的目标。

---

## 2. 这个包接收什么

### 2.1 输入 1：Jetson UDP V2

Jetson 每次推理完成后发一帧 UDP V2 给 GMK。GMK 配置里的接收端口默认是：

```yaml
udp_bind_addr: "0.0.0.0"
udp_port: 12345
```

Jetson 的目标 IP 要写成 GMK 的 IP，端口要等于 `12345` 或你自己改的端口。

### 2.2 输入 2：ROS2 请求 `/techx/vision/request`

上层包想要某个目标，就发布 `VisionRequest` 到：

```text
/techx/vision/request
```

典型例子：

```text
想要拳头武器头：class_id = 100
想要掌武器头：class_id = 101
想要矛头武器头：class_id = 102
想要红方 R2 真 KFS：class_id = 2
想要蓝方 R2 真 KFS：class_id = 5
想要二维码：class_id = 200
```

收到 request 后，`vision_bridge_node` 会从最新 `/frame` 中筛目标，并把结果发到 `/selected`。

---

## 3. 这个包发布什么

| 话题 | 类型 | 默认是否开 | 用途 |
|---|---|---|---|
| `/techx/vision/frame` | `VisionFrame` | 开 | 完整一帧视觉结果，包含所有目标，是主数据源 |
| `/techx/vision/objects` | `VisionObject` | 开 | 单目标调试流，可用于调试 |
| `/techx/vision/selected` | `VisionSelection` | 开 | 根据 `/request` 选出的当前对应目标 |
| `/techx/vision/kfs_targets` | 旧消息 | 关 | 旧兼容话题，不推荐新代码用 |
| `/techx/vision/targets` | 旧消息 | 关 | 旧兼容话题，不推荐新代码用 |

推荐：

```text
调试和复杂决策：订阅 /techx/vision/frame。
简单按阶段拿目标：发布 /request，订阅 /selected。
```

---

## 4. UDP V2 数据包格式

Jetson -> GMK 的 UDP V2 是“一帧多目标”，不是一个目标一个包。

包结构：

```text
Header 17 字节
Target 0~16 个，每个 27 字节
CRC16 2 字节
```

Header：

| 字段 | 类型 | 含义 |
|---|---|---|
| `magic` | uint16 | 固定 `0x55AB` |
| `version` | uint8 | 当前为 `2` |
| `flags` | uint8 | 预留，当前一般为 0 |
| `seq` | uint32 | Jetson 递增帧序号 |
| `timestamp` | float64 | Jetson 视觉帧时间戳 |
| `count` | uint8 | 本帧目标数量，0~16 |

Target：

| 字段 | 类型 | 含义 |
|---|---|---|
| `track_id` | uint8 | 跟踪 ID |
| `class_id` | uint8 | 全局目标类别编号 |
| `color` | uint8 | 0 未知，1 红，2 蓝 |
| `confidence` | float32 | 置信度 |
| `u` | float32 | 像素中心 u |
| `v` | float32 | 像素中心 v |
| `x` | float32 | camera_link X，单位 m |
| `y` | float32 | camera_link Y，单位 m |
| `z` | float32 | camera_link Z，单位 m |

重要语义：

```text
count=0：Jetson 和 GMK 通讯在线，但这一帧没有目标。
z=0：识别到了目标，但没有有效深度，不能用于三维控制。
长时间没有任何 UDP：Jetson/网络/GMK 链路可能断开。
```

GMK 会检查 magic、version、长度、CRC、seq。非法包会丢弃，不会发布成目标。

---

## 5. 比赛目标 class_id 分配

UDP 里不传字符串名，只传 `class_id`。因此 Jetson、GMK、决策包必须使用同一张全局编号表。

### 5.1 武器头三类

| class_id | 名称 | 中文 | target_type | zone_id | 默认 control_frame |
|---:|---|---|---:|---:|---|
| 100 | `weapon_head_fist` | 拳头 | 1 | 1 | arm1_base |
| 101 | `weapon_head_palm` | 掌 | 1 | 1 | arm1_base |
| 102 | `weapon_head_spear` | 矛头 | 1 | 1 | arm1_base |

底盘靠近武器头时用 `robot_x/y/z`，机械臂1抓取时用 `arm1_x/y/z`。

### 5.2 KFS 六类

| class_id | 名称 | 中文 | color | target_type | zone_id | 默认 control_frame |
|---:|---|---|---:|---:|---:|---|
| 0 | `kfs_red_r1` | 红方 R1 KFS | 1 | 2 | 2 | arm2_base |
| 1 | `kfs_red_r2_fake` | 红方 R2 假 KFS | 1 | 2 | 2 | arm2_base |
| 2 | `kfs_red_r2_true` | 红方 R2 真 KFS | 1 | 2 | 2 | arm2_base |
| 3 | `kfs_blue_r1` | 蓝方 R1 KFS | 2 | 2 | 2 | arm2_base |
| 4 | `kfs_blue_r2_fake` | 蓝方 R2 假 KFS | 2 | 2 | 2 | arm2_base |
| 5 | `kfs_blue_r2_true` | 蓝方 R2 真 KFS | 2 | 2 | 2 | arm2_base |

梅花林阶段通常不要只筛 `target_type=2`，应该精确筛：

```text
红方 R2 真 KFS：class_id=2
蓝方 R2 真 KFS：class_id=5
```

底盘靠近 KFS 用 `robot_x/y/z`，机械臂2操作 KFS 用 `arm2_x/y/z`。

### 5.3 二维码

| class_id | 名称 | 中文 | target_type | zone_id | 默认 control_frame |
|---:|---|---|---:|---:|---|
| 200 | `qr_code` | 二维码 | 3 | 3 | robot_base |

二维码水平对齐通常用 `align_err_x/y`，靠近距离用 `robot_x/y/z` 或 `control_z`。当前协议不传二维码字符串内容。

---

## 6. 三个核心 ROS2 消息

### 6.1 `VisionFrame`：完整一帧

话题：

```text
/techx/vision/frame
```

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

### 6.2 `VisionRequest`：上层请求

话题：

```text
/techx/vision/request
```

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
| `require_control_xyz` | 是否要求目标必须有有效三维控制坐标 |
| `min_confidence` | 最低置信度 |
| `max_frame_age_sec` | 允许使用的最大视觉帧年龄，单位秒，0 使用默认值 |

### 6.3 `VisionSelection`：请求对应结果

话题：

```text
/techx/vision/selected
```

字段：

| 字段 | 含义 |
|---|---|
| `frame_seq` | 结果来自哪一帧视觉数据 |
| `request_seq` | 对应哪个 request |
| `has_request` | 是否已经收到 request |
| `has_match` | 是否找到符合 request 的目标 |
| `status` | 当前状态 |
| `selected_index` | 选中的目标在 frame.targets[] 中的索引 |
| `frame_age_sec` | 当前视觉帧已经过去多久 |
| `score` | 选择评分 |
| `target` | 选中的目标，类型是 `VisionObject` |

`status`：

| 数值 | 含义 | 能不能控制 |
|---:|---|---|
| 0 | 正常找到目标 | 继续检查坐标有效后可用 |
| 1 | 没收到 request | 不能用 |
| 2 | 没收到视觉帧 | 不能用 |
| 3 | 没有匹配目标 | 不能用 |
| 4 | 视觉帧太旧 | 不能用 |
| 5 | request 太旧 | 不能用 |

使用 `/selected` 控制前必须检查：

```text
status == 0
has_match == true
```

如果需要三维控制，还必须检查：

```text
target.valid_control_xyz == true
```

---

## 7. `VisionObject` 每个目标字段

| 字段 | 含义 |
|---|---|
| `zone_id` | 目标区域，1 武器头，2 KFS，3 二维码 |
| `target_type` | 目标类型，1 武器头，2 KFS，3 二维码 |
| `class_id` | 具体目标编号，最重要 |
| `color` | 颜色，0 未知，1 红，2 蓝 |
| `confidence` | 识别置信度 |
| `u/v` | 像素中心 |
| `valid_xyz` | camera_link 坐标是否有效 |
| `x/y/z` | camera_link 坐标，单位 m |
| `valid_robot_xyz` | robot_base 坐标是否有效 |
| `robot_x/y/z` | robot_base 坐标，底盘/导航用 |
| `valid_arm1_xyz` | arm1_base 坐标是否有效 |
| `arm1_x/y/z` | arm1_base 坐标，机械臂1用 |
| `valid_arm2_xyz` | arm2_base 坐标是否有效 |
| `arm2_x/y/z` | arm2_base 坐标，机械臂2用 |
| `control_frame` | 推荐控制坐标系 |
| `valid_control_xyz` | 推荐控制坐标是否有效 |
| `control_x/y/z` | 推荐控制坐标 |
| `align_err_x/y` | 相对图像中心的归一化误差，常用于对齐 |
| `priority` | 选择优先级 |

使用原则：

```text
底盘：用 robot_x/y/z。
机械臂1：用 arm1_x/y/z。
机械臂2：用 arm2_x/y/z。
简单快速使用：可用 control_x/y/z，但要先看 control_frame。
```

---

## 8. 坐标系和外参

四个坐标系：

```text
camera_link：Jetson 相机坐标系，UDP 里的 x/y/z 属于它。
robot_base：机器人本体坐标系，底盘/导航用。
arm1_base：机械臂1基座坐标系，武器头抓取用。
arm2_base：机械臂2基座坐标系，KFS 操作用。
```

本包负责：

```text
p_robot = T_robot_camera * p_camera
p_arm1  = T_arm1_robot  * p_robot
p_arm2  = T_arm2_robot  * p_robot
```

其他包不要重复做 `camera_link -> robot_base/arm_base`。如果导航包要把 `robot_base` 转到 `map/odom`，那属于导航定位问题，不属于这个包的相机外参转换。

---

## 9. 配置文件 `vision_bridge.yaml`

路径：

```text
src/techx_vision_bridge/config/vision_bridge.yaml
```

当前只有一个顶层节点：

```yaml
vision_bridge_node:
  ros__parameters:
```

不要再写 `vision_selector_node` 顶层配置，因为现在已经合并成单节点。

关键参数：

```yaml
udp_bind_addr: "0.0.0.0"
udp_port: 12345
frame_topic_name: "/techx/vision/frame"
object_topic_name: "/techx/vision/objects"
request_topic_name: "/techx/vision/request"
selected_topic_name: "/techx/vision/selected"
enable_request_selector: true
reliable_qos: true
qos_depth: 5
image_width: 640.0
image_height: 480.0
```

### 9.1 class_rules

```yaml
class_rules:
  - "0-5:2:2:4:0.0"
  - "100-102:1:1:3:0.0"
  - "200:3:3:2:0.0"
```

格式：

```text
"class_or_range:zone_id:target_type:control_frame:priority_bias"
```

`control_frame`：

```text
1=camera_link
2=robot_base
3=arm1_base
4=arm2_base
```

### 9.2 外参

```yaml
enable_transforms: true
T_robot_camera_xyz_rpy: [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
T_arm1_robot_xyz_rpy:  [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
T_arm2_robot_xyz_rpy:  [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
```

格式是：

```text
[x, y, z, roll, pitch, yaw]
```

单位：

```text
x/y/z：米
roll/pitch/yaw：弧度
```

方向：

```text
T_robot_camera：point_robot = R * point_camera + t
T_arm1_robot： point_arm1  = R * point_robot  + t
T_arm2_robot： point_arm2  = R * point_robot  + t
```

### 9.3 断联保护

```yaml
watchdog_timeout_sec: 0.3
fatal_no_udp_timeout_sec: 600.0
```

含义：

```text
watchdog_timeout_sec：短时间无 UDP，只报警，并让 /selected 变成 FRAME_STALE。
fatal_no_udp_timeout_sec：长时间无有效 UDP，节点主动 shutdown。600 秒 = 10 分钟，0 表示禁用。
```

想改成 5 分钟自动退出：

```yaml
fatal_no_udp_timeout_sec: 300.0
```

### 9.4 request/selected 行为

```yaml
request_timeout_sec: 0.0
default_max_frame_age_sec: 0.20
publish_period_ms: 50
```

含义：

```text
request_timeout_sec=0：一直保留最新 request，直到上层发新的。
default_max_frame_age_sec：selected 最多使用多旧的 frame。
publish_period_ms：收到 request 后周期发布 selected 状态，即使没有新 frame 也会发布 stale/no-frame 状态。
```

---

## 10. 小白使用流程

### 10.1 编译

```bash
cd ~/gmk_ws
rm -rf build install log
colcon build --packages-select techx_vision_bridge
source install/setup.bash
```

### 10.2 检查配置

```bash
python3 src/techx_vision_bridge/tools/check_vision_bridge_config.py \
  --config src/techx_vision_bridge/config/vision_bridge.yaml
```

### 10.3 启动 GMK 视觉包

```bash
ros2 launch techx_vision_bridge vision_bridge.launch.py
```

### 10.4 没有 Jetson 时，用 mock 测试

```bash
ros2 run techx_vision_bridge mock_jetson_sender.py --mode mixed --ip 127.0.0.1
```

看完整帧：

```bash
ros2 topic echo /techx/vision/frame
```

### 10.5 请求一个目标

请求二维码：

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

---

## 11. 常用 request 示例

拳头武器头：

```bash
ros2 topic pub --once /techx/vision/request techx_vision_bridge/msg/VisionRequest "{request_seq: 1, target_type: 1, zone_id: 1, use_class_id: true, class_id: 100, use_color: false, require_control_xyz: true, min_confidence: 0.4, max_frame_age_sec: 0.2}"
```

掌武器头：

```bash
ros2 topic pub --once /techx/vision/request techx_vision_bridge/msg/VisionRequest "{request_seq: 2, target_type: 1, zone_id: 1, use_class_id: true, class_id: 101, use_color: false, require_control_xyz: true, min_confidence: 0.4, max_frame_age_sec: 0.2}"
```

矛头武器头：

```bash
ros2 topic pub --once /techx/vision/request techx_vision_bridge/msg/VisionRequest "{request_seq: 3, target_type: 1, zone_id: 1, use_class_id: true, class_id: 102, use_color: false, require_control_xyz: true, min_confidence: 0.4, max_frame_age_sec: 0.2}"
```

红方 R2 真 KFS：

```bash
ros2 topic pub --once /techx/vision/request techx_vision_bridge/msg/VisionRequest "{request_seq: 4, target_type: 2, zone_id: 2, use_class_id: true, class_id: 2, use_color: false, require_control_xyz: true, min_confidence: 0.4, max_frame_age_sec: 0.2}"
```

蓝方 R2 真 KFS：

```bash
ros2 topic pub --once /techx/vision/request techx_vision_bridge/msg/VisionRequest "{request_seq: 5, target_type: 2, zone_id: 2, use_class_id: true, class_id: 5, use_color: false, require_control_xyz: true, min_confidence: 0.4, max_frame_age_sec: 0.2}"
```

二维码：

```bash
ros2 topic pub --once /techx/vision/request techx_vision_bridge/msg/VisionRequest "{request_seq: 6, target_type: 3, zone_id: 3, use_class_id: true, class_id: 200, use_color: false, require_control_xyz: false, min_confidence: 0.3, max_frame_age_sec: 0.2}"
```

---

## 12. 手眼/外参标定怎么配合

标定采集建议在 Jetson 端做，因为 Jetson 能拿到相机图像、深度和 camera_link 点。最终标定结果手动填写到 GMK 的 `vision_bridge.yaml`。

流程：

```text
1. Jetson UI 或标定工具采集 camera_link 点。
2. 人工测量、机械臂或底盘提供同一点在 robot_base/arm_base 下的坐标。
3. 用多点拟合工具求 T_robot_camera 或其他外参。
4. 把 [x,y,z,roll,pitch,yaw] 填到 GMK 的 vision_bridge.yaml。
5. 重启 GMK vision_bridge_node。
6. 看 /techx/vision/frame 中 robot_x/arm1_x/arm2_x 是否合理。
```

GMK 端也安装了外参估计脚本，可直接运行：

```bash
ros2 run techx_vision_bridge estimate_extrinsic_from_points.py --csv robot_camera_points.csv --name T_robot_camera
```

---

## 13. 稳定性和安全要求

决策包或通讯包在使用视觉数据前必须检查：

```text
/selected.status == 0
/selected.has_match == true
需要三维控制时 target.valid_control_xyz == true
frame_age_sec 不要太大
confidence 过阈值
目标坐标不要突然跳变
最好连续 3~5 帧稳定再动作
```

如果 `/selected.status=4`，说明视觉帧太旧，不要继续控制。

如果 GMK 10 分钟收不到 Jetson UDP，会自动 shutdown。上层可以用 systemd、ros launch 或人工方式重启。

---

## 14. 新增识别物体怎么扩展

1. Jetson 给新目标分配新的全局 `class_id` 或范围，例如 `150~159`。
2. GMK `vision_bridge.yaml` 添加规则：

```yaml
class_rules:
  - "150-159:10:10:2:0.0"
```

3. 决策包发布对应 request：

```text
target_type=10
zone_id=10
```

或者精确指定：

```text
use_class_id=true
class_id=150
```

不需要改 UDP 协议，也不需要让下游包自己解析 Jetson 数据。

---

## 15. 最终记忆版

```text
这个包只有一个节点：vision_bridge_node。
它收 Jetson UDP，也收 /techx/vision/request。
它发完整 /techx/vision/frame，也发请求结果 /techx/vision/selected。
想要什么目标，就发对应 class_id 的 request。
底盘用 robot_x/y/z。
机械臂1用 arm1_x/y/z。
机械臂2用 arm2_x/y/z。
外参结果填 vision_bridge.yaml。
长时间收不到 Jetson 数据会自动退出。
```
