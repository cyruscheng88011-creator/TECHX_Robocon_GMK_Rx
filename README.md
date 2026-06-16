# techx_vision_bridge 使用说明（GMK 端视觉桥接包）

这份 README 只讲 **GMK 仓库里的 `techx_vision_bridge` 包是什么、怎么用、需要配置什么参数、通讯数据是什么含义、其他包应该如何拿数据**。

它面向第一次接触这个工程的人。你不需要先理解所有 C++ 源码，只要按本文顺序走，就能知道这个包在整车里处于什么位置，以及决策包、底盘包、机械臂包应该怎么用它。

---

## 0. 一句话说明这个包

`techx_vision_bridge` 是 **GMK 端 ROS 2 视觉传感器桥接包**。

它做的事情是：

```text
接收 Jetson 发来的 UDP V2 视觉数据包
        ↓
检查数据是否合法
        ↓
把目标 class_id 转成比赛语义
        ↓
把 Jetson 的 camera_link 坐标转换成 robot_base / arm1_base / arm2_base
        ↓
发布 ROS 2 话题，给决策包、底盘包、机械臂包、通讯包使用
```

它不做的事情：

```text
不训练 YOLO 模型
不驱动相机
不直接控制底盘电机
不直接控制机械臂关节
不做导航路径规划
不做机械臂逆解
不替代决策状态机
```

你可以把它理解成一个“智能视觉传感器”：

```text
Jetson 是相机和识别端
techx_vision_bridge 是 GMK 上的视觉数据接收和坐标转换端
决策包根据视觉数据决定下一步动作
通讯包/底盘包/机械臂包执行决策包给出的控制命令
```

---

## 1. 整车软件数据流

### 1.1 最完整的数据流

```text
Jetson + RGB-D 相机
  │
  │  识别武器头、KFS、二维码
  │  查询深度
  │  算出目标在 camera_link 下的 x/y/z
  │  通过 UDP V2 发给 GMK
  ▼
GMK: vision_bridge_node
  │
  │  接收 UDP
  │  校验 magic / version / length / CRC / seq
  │  根据 class_id 判断目标类型
  │  统一计算 robot_base / arm1_base / arm2_base 坐标
  │  发布 /techx/vision/frame
  ▼
GMK: vision_selector_node，可选但推荐
  │
  │  接收决策包发来的 /techx/vision/request
  │  从最新 /techx/vision/frame 中筛选当前最优目标
  │  发布 /techx/vision/selected
  ▼
决策包
  │
  │  读取 /techx/vision/frame 或 /techx/vision/selected
  │  判断当前任务阶段应该做什么
  │  生成底盘、机械臂、吸盘、夹爪等动作命令
  ▼
通讯包 / 底盘控制包 / 机械臂控制包
  │
  │  把决策命令发给下位机或执行器
  ▼
电机 / 机械臂 / 底盘 / 吸盘 / 夹爪
```

### 1.2 决策包和通讯包是不是要给 GMK 发指令？

分清两件事：

#### 第一，视觉数据不是靠通讯包请求才有

`vision_bridge_node` 会一直接收 Jetson 数据，并一直发布 `/techx/vision/frame`。

所以 GMK 视觉包不是这种模式：

```text
通讯包发指令：我要武器头
GMK 才去找武器头
```

正确模式是：

```text
Jetson 持续识别
GMK 持续发布完整视觉帧
决策包根据当前任务阶段选择要使用哪个目标
```

#### 第二，决策包可以给 selector 发“筛选请求”

如果决策包不想自己遍历 `/techx/vision/frame` 里的所有目标，可以发布 `/techx/vision/request`。

例如：

```text
决策包说：我当前只关心 class_id=100 的拳头武器头
vision_selector_node 就从最新 frame 中筛出最好的 class_id=100 目标
然后发布到 /techx/vision/selected
```

注意：

```text
/request 不是命令 Jetson 只识别某个目标
/request 只是让 GMK selector 从已经收到的完整 frame 里筛目标
```

### 1.3 推荐使用方式

初学者先用主接口：

```text
/techx/vision/frame
```

等你确认数据都对了，再用便捷接口：

```text
/techx/vision/request
/techx/vision/selected
```

推荐分工：

```text
调试、日志、复杂决策：看 /techx/vision/frame
简单按任务阶段取当前目标：用 /request + /selected
底盘和机械臂最终动作：由决策包发给通讯包或控制包，不由 vision_bridge 直接控制
```

---

## 2. 这个包里的两个节点

### 2.1 `vision_bridge_node`，必须运行

这是核心节点。

它负责：

```text
1. 监听 UDP 端口
2. 接收 Jetson UDP V2 数据包
3. 检查数据包是否合法
4. 解码目标 class_id、confidence、u/v、camera x/y/z
5. 根据 class_rules 映射 zone_id、target_type、control_frame
6. 用外参计算 robot_base、arm1_base、arm2_base 坐标
7. 发布完整视觉帧 /techx/vision/frame
```

它的核心输出是：

```text
/techx/vision/frame
```

### 2.2 `vision_selector_node`，推荐运行

这是辅助节点。

它负责：

```text
1. 订阅 /techx/vision/frame
2. 订阅 /techx/vision/request
3. 根据最新 request，从最新 frame 里筛选一个最优目标
4. 发布 /techx/vision/selected
5. 如果视觉断流，周期发布 FRAME_STALE 状态
```

它不会直接收 UDP，也不会命令 Jetson 改模型。它只是一个“筛选器”。

---

## 3. 三个核心话题

## 3.1 `/techx/vision/frame`，主数据源

类型：

```text
techx_vision_bridge/msg/VisionFrame
```

含义：一帧完整视觉结果。

字段：

| 字段 | 含义 |
|---|---|
| `header` | ROS 2 消息头，包含时间和 frame_id |
| `seq` | Jetson UDP V2 帧序号 |
| `protocol_version` | 协议版本，当前是 2 |
| `upstream_timestamp` | Jetson 相机/推理端时间戳 |
| `target_count` | 本帧目标数量 |
| `has_target` | 本帧是否有目标 |
| `targets[]` | 本帧所有目标，每个元素是 `VisionObject` |

重要语义：

```text
target_count == 0 且 has_target == false
表示 Jetson 和 GMK 链路在线，但这一帧没有识别到新目标。
```

如果长时间没有任何 `/techx/vision/frame` 消息，才说明 Jetson、网络或 GMK 接收链路可能断了。

---

## 3.2 `/techx/vision/request`，决策筛选请求

类型：

```text
techx_vision_bridge/msg/VisionRequest
```

含义：决策包告诉 selector 当前关心什么目标。

字段：

| 字段 | 含义 |
|---|---|
| `header` | ROS 2 消息头 |
| `request_seq` | 决策包自己递增的请求序号，selector 会原样返回 |
| `target_type` | 目标大类，0 表示不限 |
| `zone_id` | 目标区域，0 表示不限 |
| `use_class_id` | 是否启用精确 class_id 筛选 |
| `class_id` | 具体目标编号，例如 100=拳头，2=红方 R2 真 KFS，200=二维码 |
| `use_color` | 是否启用颜色筛选 |
| `color` | 0=未知，1=红，2=蓝 |
| `require_control_xyz` | 是否要求目标必须有有效三维控制坐标 |
| `min_confidence` | 最低置信度，小于该值不选 |
| `max_frame_age_sec` | 最老允许使用多少秒前的视觉帧，0 表示使用 selector 默认值 |

`target_type` 常量：

| 数值 | 含义 |
|---:|---|
| 0 | 任意类型 |
| 1 | 武器头 |
| 2 | KFS |
| 3 | 二维码 |
| 10 | 自定义目标 |

`zone_id` 常量：

| 数值 | 含义 |
|---:|---|
| 0 | 任意区域 |
| 1 | 武器头区域 |
| 2 | KFS 区域 |
| 3 | 二维码区域 |
| 10 | 自定义区域 |

---

## 3.3 `/techx/vision/selected`，当前最优目标

类型：

```text
techx_vision_bridge/msg/VisionSelection
```

含义：selector 根据最新 request 选出的当前最优目标。

字段：

| 字段 | 含义 |
|---|---|
| `header` | ROS 2 消息头 |
| `frame_seq` | 这个结果来自哪一帧视觉数据 |
| `request_seq` | 对应哪个决策请求 |
| `has_request` | selector 是否已经收到 request |
| `has_match` | 当前 request 是否匹配到了目标 |
| `status` | 当前选择状态 |
| `selected_index` | 被选中的目标在 frame.targets[] 中的索引 |
| `frame_age_sec` | 当前使用的视觉帧距离现在有多久 |
| `score` | selector 给出的选择评分 |
| `target` | 被选中的目标，类型是 `VisionObject` |

`status` 含义：

| 数值 | 名称 | 含义 | 能否控制 |
|---:|---|---|---|
| 0 | `STATUS_OK` | 正常，找到可用目标 | 可以继续检查坐标有效性后使用 |
| 1 | `STATUS_NO_REQUEST` | 还没有收到决策请求 | 不要控制 |
| 2 | `STATUS_NO_FRAME` | 还没有收到视觉帧 | 不要控制 |
| 3 | `STATUS_NO_MATCH` | 有视觉帧，但没有符合 request 的目标 | 不要控制 |
| 4 | `STATUS_FRAME_STALE` | 视觉帧太旧 | 不要控制 |
| 5 | `STATUS_REQUEST_STALE` | 请求太旧，当前配置默认不会触发，除非启用 request_timeout | 不要控制 |

控制前必须检查：

```text
has_request == true
has_match == true
status == STATUS_OK
```

如果要用三维坐标控制，还必须检查：

```text
target.valid_control_xyz == true
```

---

## 4. `VisionObject` 每个字段是什么意思

`VisionObject` 是最重要的目标数据结构。`/frame.targets[]` 里面每个目标都是一个 `VisionObject`，`/selected.target` 也是一个 `VisionObject`。

### 4.1 目标身份字段

| 字段 | 含义 |
|---|---|
| `seq` | 目标来自哪一帧 |
| `target_index` | 这个目标在本帧中的编号 |
| `target_count` | 本帧总目标数量 |
| `zone_id` | 目标区域，例如武器头/KFS/二维码 |
| `target_type` | 目标大类，例如武器头/KFS/二维码 |
| `class_id` | 具体目标编号，这是最关键的类别字段 |
| `color` | 颜色，0=未知，1=红，2=蓝 |
| `confidence` | 识别置信度 |

### 4.2 像素字段

| 字段 | 含义 |
|---|---|
| `u` | 图像横向像素中心 |
| `v` | 图像纵向像素中心 |
| `align_err_x` | 相对图像中心的横向归一化误差，通常用于底盘水平对齐 |
| `align_err_y` | 相对图像中心的纵向归一化误差 |

`align_err_x/y` 适合二维码或目标粗对齐，但它不是三维位置。

### 4.3 原始相机坐标

| 字段 | 含义 |
|---|---|
| `valid_xyz` | Jetson 是否给出了有效 camera_link 坐标 |
| `x/y/z` | 目标在 `camera_link` 下的位置，单位 m |

`camera_link` 是相机坐标，通常 RGB-D 相机约定是：

```text
X：图像右方
Y：图像下方
Z：相机前方
```

具体以 Jetson 视觉端实现为准。

### 4.4 机器人本体坐标

| 字段 | 含义 |
|---|---|
| `valid_robot_xyz` | 是否成功转换到 robot_base |
| `robot_x/y/z` | 目标在 `robot_base` 下的位置，单位 m |

底盘、导航、二维码靠近、根据武器头/KFS 调整机器人位置，都应该用 `robot_x/y/z`。

### 4.5 机械臂1坐标

| 字段 | 含义 |
|---|---|
| `valid_arm1_xyz` | 是否成功转换到 arm1_base |
| `arm1_x/y/z` | 目标在 `arm1_base` 下的位置，单位 m |

机械臂1抓武器头、武器头对接动作，应使用 `arm1_x/y/z`。

### 4.6 机械臂2坐标

| 字段 | 含义 |
|---|---|
| `valid_arm2_xyz` | 是否成功转换到 arm2_base |
| `arm2_x/y/z` | 目标在 `arm2_base` 下的位置，单位 m |

机械臂2操作 KFS，应使用 `arm2_x/y/z`。

### 4.7 推荐控制坐标

| 字段 | 含义 |
|---|---|
| `control_frame` | 推荐使用哪个坐标系 |
| `valid_control_xyz` | 推荐坐标是否有效 |
| `control_x/y/z` | 推荐控制坐标，单位 m |

`control_frame` 数值：

| 数值 | 坐标系 |
|---:|---|
| 0 | unknown |
| 1 | camera_link |
| 2 | robot_base |
| 3 | arm1_base |
| 4 | arm2_base |

注意：`control_x/y/z` 只是默认推荐字段，不是唯一字段。

更稳的实际用法是：

```text
底盘控制：始终看 robot_x/y/z
机械臂1：始终看 arm1_x/y/z
机械臂2：始终看 arm2_x/y/z
简单任务：可以直接看 control_x/y/z
```

---

## 5. 比赛目标 class_id 分配

UDP V2 中不传字符串名称，只传 `class_id`。所以 `class_id` 必须固定，不能随着模型训练顺序变化。

### 5.1 武器头，三种

| class_id | 名称 | 中文 | target_type | zone_id | 推荐主坐标系 |
|---:|---|---|---:|---:|---|
| 100 | `weapon_head_fist` | 拳头 | 1 | 1 | arm1_base |
| 101 | `weapon_head_palm` | 掌 | 1 | 1 | arm1_base |
| 102 | `weapon_head_spear` | 矛头 | 1 | 1 | arm1_base |

使用建议：

```text
底盘靠近武器头：用 robot_x/y/z
机械臂1抓取武器头：用 arm1_x/y/z
如果只想快速拿默认坐标：用 control_x/y/z，control_frame 应为 3
```

### 5.2 KFS，六种

| class_id | 名称 | 中文 | color | target_type | zone_id | 推荐主坐标系 |
|---:|---|---|---:|---:|---:|---|
| 0 | `kfs_red_r1` | 红方 R1 KFS | 1 | 2 | 2 | arm2_base |
| 1 | `kfs_red_r2_fake` | 红方 R2 假 KFS | 1 | 2 | 2 | arm2_base |
| 2 | `kfs_red_r2_true` | 红方 R2 真 KFS | 1 | 2 | 2 | arm2_base |
| 3 | `kfs_blue_r1` | 蓝方 R1 KFS | 2 | 2 | 2 | arm2_base |
| 4 | `kfs_blue_r2_fake` | 蓝方 R2 假 KFS | 2 | 2 | 2 | arm2_base |
| 5 | `kfs_blue_r2_true` | 蓝方 R2 真 KFS | 2 | 2 | 2 | arm2_base |

梅花林阶段通常不应该只筛 `target_type=2`，因为这会同时包含 R1、R2 假、R2 真。更推荐精确筛：

```text
红方 R2 真 KFS：class_id = 2
蓝方 R2 真 KFS：class_id = 5
```

使用建议：

```text
底盘靠近 KFS：用 robot_x/y/z
机械臂2操作 KFS：用 arm2_x/y/z
如果只想快速拿默认坐标：用 control_x/y/z，control_frame 应为 4
```

### 5.3 二维码

| class_id | 名称 | 中文 | target_type | zone_id | 推荐主坐标系 |
|---:|---|---|---:|---:|---|
| 200 | `qr_code` | 二维码 | 3 | 3 | robot_base |

当前 UDP V2 只传二维码中心、置信度和三维点，不传二维码字符串内容。

如果只需要对齐和靠近，当前足够：

```text
水平对齐：align_err_x / align_err_y
距离靠近：robot_x/y/z 或 control_z
```

如果以后要读取二维码里的字符串，比如 R1 编号、任务状态、拼接结果，就需要扩展协议。

---

## 6. UDP V2 通讯数据包

### 6.1 Jetson 发什么

Jetson 每次推理完成后，通过 UDP V2 发一帧完整视觉结果。

包结构：

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
| `flags` | uint8 | 当前预留，一般为 0 |
| `seq` | uint32 | 帧序号 |
| `timestamp` | double | Jetson 时间戳 |
| `count` | uint8 | 本帧目标数量，0~16 |

Target：

| 字段 | 类型 | 含义 |
|---|---|---|
| `track_id` | uint8 | 跟踪 ID |
| `class_id` | uint8 | 全局类别编号 |
| `color` | uint8 | 0 未知，1 红，2 蓝 |
| `confidence` | float32 | 置信度 |
| `u` | float32 | 像素中心 u |
| `v` | float32 | 像素中心 v |
| `x` | float32 | camera_link X，单位 m |
| `y` | float32 | camera_link Y，单位 m |
| `z` | float32 | camera_link Z，单位 m |

包末尾：

| 字段 | 类型 | 含义 |
|---|---|---|
| `crc16` | uint16 | CRC 校验 |

完整长度：

```text
17 + count * 27 + 2
```

### 6.2 `count=0` 是什么意思

```text
count=0
```

表示：

```text
Jetson 和 GMK 通讯在线，但这一帧没有识别到目标。
```

这和“没有收到任何 UDP 包”不同。

### 6.3 `z=0` 是什么意思

如果某个目标有识别框，但没有有效深度，Jetson 可能发送：

```text
x=0, y=0, z=0
```

GMK 会把对应 `valid_xyz / valid_robot_xyz / valid_arm1_xyz / valid_arm2_xyz / valid_control_xyz` 标成无效。

决策包不能只看识别到了目标，还要检查坐标是否有效。

---

## 7. 坐标系和外参

### 7.1 四个坐标系

#### `camera_link`

相机坐标系。Jetson UDP 里的 `x/y/z` 属于它。

常见 RGB-D 相机约定：

```text
X：图像右方
Y：图像下方
Z：相机前方
```

#### `robot_base`

机器人本体坐标系。建议统一为：

```text
X：机器人前方
Y：机器人左方
Z：机器人上方
```

底盘控制、二维码对齐、武器头/KFS 相对机器人位置，都用它。

#### `arm1_base`

机械臂1基座坐标系。武器头抓取和对接时，机械臂1用它。

#### `arm2_base`

机械臂2基座坐标系。KFS 操作时，机械臂2用它。

### 7.2 本包负责哪些转换

本包负责：

```text
camera_link -> robot_base
robot_base  -> arm1_base
robot_base  -> arm2_base
```

公式：

```text
p_robot = T_robot_camera * p_camera
p_arm1  = T_arm1_robot  * p_robot
p_arm2  = T_arm2_robot  * p_robot
```

### 7.3 其他包还要不要再转换

不要再重复做 `camera_link -> robot_base/arm_base`。

其他包应该直接使用本包输出的：

```text
robot_x/y/z
arm1_x/y/z
arm2_x/y/z
```

但是如果导航包要把 `robot_base` 下的点转换到 `map` 或 `odom`，那属于导航定位问题，不属于本包的相机外参转换。

---

## 8. 配置文件 `vision_bridge.yaml`

配置文件路径：

```text
src/techx_vision_bridge/config/vision_bridge.yaml
```

它有两个顶层节点：

```yaml
vision_bridge_node:
  ros__parameters:
    ...

vision_selector_node:
  ros__parameters:
    ...
```

注意：顶层名字必须和节点名字一致，否则参数不会生效。

### 8.1 UDP 参数

```yaml
udp_bind_addr: "0.0.0.0"
udp_port: 12345
```

含义：

| 参数 | 含义 |
|---|---|
| `udp_bind_addr` | GMK 监听哪个本地地址，通常用 `0.0.0.0` |
| `udp_port` | GMK 接收 Jetson UDP 的端口，必须和 Jetson 发送端一致 |

### 8.2 话题名字

```yaml
frame_topic_name: "/techx/vision/frame"
object_topic_name: "/techx/vision/objects"
detail_topic_name: "/techx/vision/kfs_targets"
topic_name: "/techx/vision/targets"
```

推荐主接口：

```text
/techx/vision/frame
/techx/vision/selected
```

`/techx/vision/objects` 可用于调试单目标流。

`/techx/vision/kfs_targets` 和 `/techx/vision/targets` 是旧兼容话题，默认关闭，不推荐新代码使用。

### 8.3 发布开关

```yaml
publish_frame_topic: true
publish_object_topic: true
publish_detail_topic: false
publish_legacy_topic: false
accept_legacy: false
```

含义：

| 参数 | 建议 | 含义 |
|---|---|---|
| `publish_frame_topic` | true | 发布完整帧，必须开 |
| `publish_object_topic` | true | 发布单目标调试流，可开 |
| `publish_detail_topic` | false | 旧 KFS 详情话题，默认关 |
| `publish_legacy_topic` | false | 旧单目标话题，默认关 |
| `accept_legacy` | false | 是否接收旧 29 字节协议，比赛建议关 |

### 8.4 QoS 参数

```yaml
reliable_qos: true
qos_depth: 5
```

建议保持默认。

`reliable_qos=true` 可以让普通决策包、导航包更容易收到数据，不需要特意使用 `SensorDataQoS`。

### 8.5 图像尺寸参数

```yaml
image_width: 640.0
image_height: 480.0
```

用于计算 `align_err_x/y`。

如果 Jetson 实际识别图像不是 640x480，要改成实际尺寸。

### 8.6 class_rules

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

字段含义：

| 字段 | 含义 |
|---|---|
| `class_or_range` | 单个 class_id 或范围，例如 `200`、`0-5` |
| `zone_id` | 区域编号 |
| `target_type` | 目标类型 |
| `control_frame` | 推荐控制坐标系 |
| `priority_bias` | 优先级偏置，默认 0 |

`control_frame` 数值：

| 数值 | 坐标系 |
|---:|---|
| 1 | camera_link |
| 2 | robot_base |
| 3 | arm1_base |
| 4 | arm2_base |

当前默认规则含义：

```text
0~5：KFS，推荐 arm2_base
100~102：武器头，推荐 arm1_base
200：二维码，推荐 robot_base
```

### 8.7 外参参数

```yaml
enable_transforms: true
T_robot_camera_xyz_rpy: [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
T_arm1_robot_xyz_rpy:  [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
T_arm2_robot_xyz_rpy:  [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
```

格式：

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

如果你测到的是反方向，例如 `T_camera_robot`，不能直接填，需要先求逆。

### 8.8 watchdog 参数

```yaml
watchdog_timeout_sec: 0.3
```

含义：超过 0.3 秒没有收到 Jetson 数据时，bridge 会认为视觉链路可能断流。

真正的停车或停止机械臂动作仍然应该由决策包执行。

### 8.9 selector 参数

```yaml
request_timeout_sec: 0.0
default_max_frame_age_sec: 0.20
publish_period_ms: 50
```

含义：

| 参数 | 含义 |
|---|---|
| `request_timeout_sec` | 请求超时。0 表示一直使用最新 request，直到决策包发新 request |
| `default_max_frame_age_sec` | selector 默认最多使用多旧的视觉帧 |
| `publish_period_ms` | selector 周期发布 selected 状态，即使没有新 frame 也会发布 stale 状态 |

---

## 9. 手眼标定 / 外参标定到底怎么理解

### 9.1 它是不是 Jetson 端操作

不应该简单说“是 Jetson 端”或“不是 Jetson 端”。正确理解是：

```text
Jetson 参与采集标定数据
GMK 使用标定结果
机械臂或底盘可能参与移动标定板/目标点
```

Jetson 的作用：

```text
拍到标定板、二维码、球心、角点或其他标定目标
输出这些点在 camera_link 下的坐标
```

机械臂/底盘的作用：

```text
把标定板或目标点移动到已知位置
或者让末端/TCP 去触碰已知点
提供 robot_base / arm_base 下的对应坐标
```

GMK 的作用：

```text
保存 T_robot_camera / T_arm1_robot / T_arm2_robot
运行时用这些外参转换坐标
```

所以：

```text
标定采集可以在 Jetson 上完成
标定计算可以在 Jetson、GMK 或电脑上完成
标定结果最终应该写入 GMK 的 vision_bridge.yaml
```

### 9.2 为什么不是只在 Jetson 里写死外参

不建议把 robot/arm 外参写死在 Jetson 视觉程序里，原因：

```text
1. Jetson 只负责视觉识别和 camera_link 坐标，更清晰
2. GMK 是 ROS 2 中央数据桥，所有下游包都从这里拿统一坐标
3. 相机安装位置改了，只改 GMK YAML
4. 机械臂1/机械臂2基座位置改了，也只改 GMK YAML
5. 避免 Jetson、决策包、机械臂包各自维护一份外参
```

### 9.3 需要标定板吗

如果想精确，建议用标定板或固定标定目标。

常见方式：

```text
方式 A：标定板 / ArUco / AprilTag / 棋盘格
  相机识别标定板角点或 tag 位姿
  结合标定板在 robot_base 下的位置
  求 T_robot_camera

方式 B：已知 3D 点
  在机器人前方放多个已知位置点
  Jetson 读取每个点的 camera_link 坐标
  人工测量每个点的 robot_base 坐标
  用多点拟合求 T_robot_camera

方式 C：机械臂辅助采点
  机械臂末端夹一个标定点或触碰目标点
  机械臂运动学给出点在 arm_base 下的位置
  Jetson 给出同一点在 camera_link 下的位置
  结合 T_arm_robot 或同时拟合外参
```

你说的“标定板 + 控制机械臂”是可以的，尤其适合提高精度。但由于你的相机不是装在机械臂末端，所以它更准确叫：

```text
固定相机外参标定 / eye-to-base 标定
```

不是典型 eye-in-hand。

### 9.4 要标定哪些东西

必须有三个外参：

#### `T_robot_camera`

目标：

```text
p_robot = T_robot_camera * p_camera
```

用途：

```text
把 Jetson 相机坐标转换成机器人本体坐标
底盘靠近目标、二维码对齐、目标相对机器人位置都依赖它
```

#### `T_arm1_robot`

目标：

```text
p_arm1 = T_arm1_robot * p_robot
```

用途：

```text
把机器人坐标转换成机械臂1基座坐标
武器头抓取和对接依赖它
```

#### `T_arm2_robot`

目标：

```text
p_arm2 = T_arm2_robot * p_robot
```

用途：

```text
把机器人坐标转换成机械臂2基座坐标
KFS 操作依赖它
```

### 9.5 最简单的标定流程

第一步，机械测量初值：

```text
1. 定义 robot_base 坐标轴
2. 定义 camera_link 坐标轴
3. 测量相机光心相对 robot_base 的位置
4. 测量相机安装角度
5. 粗略填 T_robot_camera
6. 测量 arm1_base / arm2_base 相对 robot_base 的位置和方向
7. 粗略填 T_arm1_robot / T_arm2_robot
```

第二步，多点验证：

```text
1. 放一个目标在机器人前方已知位置
2. Jetson 读取 camera_link 下的 x/y/z
3. GMK 输出 robot_x/y/z
4. 比较 robot_x/y/z 和实际测量值
5. 如果误差很大，说明外参方向、单位或坐标轴可能错了
```

第三步，多点拟合：

```text
1. 采集 10~20 组点
2. 每组都有 p_camera 和 p_robot
3. 用 estimate_extrinsic_from_points.py 求 T_robot_camera
4. 把输出复制到 YAML
5. 再用新点验证误差
```

### 9.6 多点拟合工具

仓库提供了工具：

```text
src/techx_vision_bridge/tools/estimate_extrinsic_from_points.py
```

CSV 格式：

```csv
from_x,from_y,from_z,to_x,to_y,to_z
0.10,0.02,0.80,0.80,-0.10,0.25
0.05,0.10,1.00,1.00,-0.05,0.35
```

对于 `T_robot_camera`：

```text
from = camera_link 点
  to = robot_base 点
```

运行：

```bash
python3 src/techx_vision_bridge/tools/estimate_extrinsic_from_points.py \
  --csv robot_camera_points.csv \
  --name T_robot_camera
```

输出示例：

```yaml
T_robot_camera_xyz_rpy: [x, y, z, roll, pitch, yaw]
```

复制到 `vision_bridge.yaml`。

---

## 10. 决策包应该怎么用

### 10.1 主方式：订阅完整帧

决策包订阅：

```text
/techx/vision/frame
```

然后遍历：

```text
msg.targets[]
```

按当前任务阶段筛选目标。

### 10.2 便捷方式：request + selected

决策包发布：

```text
/techx/vision/request
```

订阅：

```text
/techx/vision/selected
```

这种方式适合每个阶段只需要一个当前最优目标。

### 10.3 请求拳头武器头

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

使用结果：

```text
底盘先看 selected.target.robot_x/y/z
机械臂1再看 selected.target.arm1_x/y/z
```

### 10.4 请求掌武器头

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

### 10.5 请求矛头武器头

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

### 10.6 请求红方 R2 真 KFS

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

使用结果：

```text
底盘先看 selected.target.robot_x/y/z
机械臂2再看 selected.target.arm2_x/y/z
```

### 10.7 请求蓝方 R2 真 KFS

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

### 10.8 请求二维码

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

使用结果：

```text
水平对齐：selected.target.align_err_x/y
靠近距离：selected.target.robot_x/y/z 或 control_z
```

如果二维码靠近阶段需要距离，必须确认：

```text
selected.target.valid_robot_xyz == true
```

---

## 11. 没有硬件时怎么测试

### 11.1 编译

```bash
cd ~/gmk_ws
rm -rf build install log
colcon build --packages-select techx_vision_bridge
source install/setup.bash
```

### 11.2 检查配置文件

```bash
python3 src/techx_vision_bridge/tools/check_vision_bridge_config.py \
  --config src/techx_vision_bridge/config/vision_bridge.yaml
```

这个脚本会检查：

```text
class_rules 是否覆盖 0~5、100~102、200
旧兼容话题是否默认关闭
T_robot_camera / T_arm1_robot / T_arm2_robot 是否存在
vision_bridge_node / vision_selector_node 参数是否存在
```

### 11.3 启动 GMK 视觉桥

```bash
ros2 launch techx_vision_bridge vision_bridge.launch.py
```

### 11.4 用 mock 模拟 Jetson

```bash
ros2 run techx_vision_bridge mock_jetson_sender.py --mode mixed --ip 127.0.0.1
```

### 11.5 看完整视觉帧

```bash
ros2 topic echo /techx/vision/frame
```

你应该看到：

```text
seq
target_count
targets[]
  class_id
  robot_x/y/z
  arm1_x/y/z
  arm2_x/y/z
  control_frame
```

### 11.6 看 selector 输出

先发布 request，例如请求二维码：

```bash
ros2 topic pub --once /techx/vision/request techx_vision_bridge/msg/VisionRequest "{
  request_seq: 10,
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

再看：

```bash
ros2 topic echo /techx/vision/selected
```

正常时应看到：

```text
status: 0
has_match: true
target:
  class_id: 200
```

---

## 12. 有硬件后怎么联调

推荐顺序：

```text
1. 先只启动 GMK，不接机械臂动作，用 mock 测通 ROS 2 话题
2. 接 Jetson，只看 /techx/vision/frame，不控制底盘和机械臂
3. 检查 class_id 是否正确
4. 检查 x/y/z 是否有有效深度
5. 填粗略外参，检查 robot_x/y/z 方向是否正确
6. 做多点外参标定
7. 低速底盘对齐测试
8. 低速机械臂空动作测试
9. 最后再做真实抓取/对接
```

不要一开始就让机械臂根据视觉坐标直接抓取。

---

## 13. 误差来自哪里

即使软件流程正确，视觉坐标也一定会有误差。

主要来源：

```text
1. Jetson 模型检测框中心误差
2. RGB-D 深度噪声
3. 深度和彩色图没有完全对齐
4. 目标表面反光或太黑导致深度空洞
5. T_robot_camera 标定误差
6. T_arm1_robot / T_arm2_robot 标定误差
7. 机械臂零点误差
8. 机械臂 TCP 偏移误差
9. 底盘运动过程中视觉延迟
10. 网络 UDP 丢包或延迟
```

所以控制前必须做安全检查：

```text
frame 不能太旧
status 必须 OK
has_match 必须 true
valid_control_xyz 必须 true
confidence 要过阈值
目标坐标不能突然跳变
最好连续 N 帧稳定后再动作
```

推荐初期阈值：

```text
max_frame_age_sec：0.2 s
min_confidence：0.3~0.5
连续稳定帧数：3~5 帧
```

---

## 14. 常见问题

### Q1：标定是不是一定要在 Jetson 上做？

不是。Jetson 提供 camera_link 坐标，标定计算可以在 Jetson、GMK 或电脑上做。最终结果要填进 GMK 的 `vision_bridge.yaml`。

### Q2：是不是一定要用标定板？

不是一定，但建议用。标定板、AprilTag、ArUco、棋盘格或固定标定点都可以。精度要求越高，越应该用标定板或机械臂辅助采点。

### Q3：为什么不让 Jetson 直接输出 arm1/arm2 坐标？

因为 Jetson 应该只负责视觉。机器人本体、机械臂1、机械臂2的外参属于整车配置，统一放在 GMK 更好维护。

### Q4：底盘控制武器头/KFS 时用哪个字段？

用：

```text
robot_x/y/z
```

不要用 `arm1_x/y/z` 或 `arm2_x/y/z` 控底盘。

### Q5：机械臂抓取时用哪个字段？

武器头：

```text
arm1_x/y/z
```

KFS：

```text
arm2_x/y/z
```

### Q6：`control_x/y/z` 能不能直接用？

可以作为简单默认值，但要先看 `control_frame`。

更推荐：

```text
底盘明确用 robot_x/y/z
机械臂1明确用 arm1_x/y/z
机械臂2明确用 arm2_x/y/z
```

### Q7：二维码内容会不会传过来？

当前不会。V2 只传二维码中心、坐标和置信度，不传字符串内容。

### Q8：如果 `/selected` 没有目标，是不是说明视觉没有看到任何东西？

不一定。它只表示“当前 request 条件下没有匹配目标”。要判断整帧有什么目标，请看 `/techx/vision/frame`。

### Q9：通讯包应该订阅哪个话题？

一般不建议通讯包直接做视觉决策。推荐：

```text
决策包订阅视觉话题
决策包计算动作命令
通讯包只负责把动作命令发给下位机
```

如果通讯包确实要直接拿视觉数据，建议订阅 `/techx/vision/frame` 或 `/techx/vision/selected`，但仍然不能跳过安全检查。

### Q10：没有 Jetson 能不能测试 GMK 包？

可以，用：

```bash
ros2 run techx_vision_bridge mock_jetson_sender.py --mode mixed --ip 127.0.0.1
```

---

## 15. 新增识别物体怎么扩展

假设后面新增一个对接标志物，分配 `class_id=150~159`。

### 15.1 Jetson 端

Jetson 模型输出必须映射到全局 class_id 150~159。

### 15.2 GMK 端

在 `vision_bridge.yaml` 中新增：

```yaml
class_rules:
  - "150-159:10:10:2:0.0"
```

含义：

```text
class_id 150~159
zone_id = 10
 target_type = 10
control_frame = 2，也就是 robot_base
```

### 15.3 决策包

发布 request：

```text
target_type = 10
zone_id = 10
```

或者精确筛：

```text
use_class_id = true
class_id = 150
```

---

## 16. 最推荐的小白使用顺序

第一次使用不要直接上车。按这个顺序：

```text
1. 看懂 README 的 0~8 节
2. colcon build 编译
3. 跑 check_vision_bridge_config.py
4. 启动 vision_bridge.launch.py
5. 用 mock_jetson_sender.py 造假数据
6. echo /techx/vision/frame
7. 发布 /techx/vision/request
8. echo /techx/vision/selected
9. 接真实 Jetson，只看数据，不控制机构
10. 做外参标定
11. 低速底盘测试
12. 低速机械臂空动作测试
13. 最后才做真实抓取/对接
```

---

## 17. 最终使用原则

记住下面几句话就不会用错：

```text
Jetson 只输出 camera_link 坐标。
GMK 负责把 camera_link 转成 robot_base、arm1_base、arm2_base。
决策包不要重复做相机外参转换。
底盘用 robot_x/y/z。
机械臂1用 arm1_x/y/z。
机械臂2用 arm2_x/y/z。
/request 只是筛选请求，不是命令 Jetson 只识别某个目标。
/selected 是便捷结果，/frame 才是完整事实。
外参结果最终填到 vision_bridge.yaml。
```
