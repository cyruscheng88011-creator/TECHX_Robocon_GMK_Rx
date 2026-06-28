# TECHX GMK 现场作业手册（vision bridge 侧）

GMK 在 Linux/ROS2 上运行：接收 Jetson 的 UDP V2 → 外参变换到机体/机械臂坐标系 →
经 `calibration_guard_node` 输出 **guarded** 话题给决策包。Jetson 侧标定/深度校准见
视觉仓库 `docs/FIELD_OPERATIONS.md`。

---

## 1. 话题与节点

```
Jetson UDP V2 ──► vision_bridge_node ──► *_raw 话题 ──► calibration_guard_node ──► 规范话题
                                                                                  /techx/vision/frame
                                                                                  /techx/vision/selected
              vision_request_demo ──► /techx/vision/request
              selection_debug_node ──► /tmp/techx_gmk_selected_debug.csv
```
- **决策包只订规范（guarded）话题** `/techx/vision/frame`、`/techx/vision/selected`，不要订 `_raw`。
- `selection_debug_node` 同时订 `_raw` 和规范话题，把“为什么没选中”写进 CSV。

## 2. 四坐标系与 class_rules

frame_id：1=camera_link 2=robot_base 3=arm1_base 4=arm2_base。`config/vision_bridge.yaml`：
```yaml
class_rules:
  - "0-5:2:2:4:0.0"        # KFS        -> arm2_base
  - "100-102:1:1:3:0.0"    # 武器头      -> arm1_base
  - "150-152:10:10:1:0.5"  # 灯带/拼接成功 -> camera_link(事件)
  - "200:3:3:2:0.0"        # 二维码      -> robot_base
```
C++ 默认规则、YAML、`check_vision_bridge_config.py` 三处保持一致（含灯带 150-152）。

## 3. 接收 Jetson 标定（安全写入）

```bash
cd <ros2_ws>   # techx_vision_bridge 工作区根
python3 src/techx_vision_bridge/tools/apply_calibration_yaml.py \
        --snippet /path/gmk_robot_camera.yaml          # 自动备份原文件
python3 src/techx_vision_bridge/tools/check_vision_bridge_config.py   # 必须输出 OK
```
- `apply` 把同一外参写进 `vision_bridge_node` 和 `calibration_guard_node` **两处**，并打开对应
  `*_calibrated` flag。两处写不全会报错。
- 片段里缺 `*_calibrated` flag 时**默认 false（fail-closed）**，不会被误判为已标定。
- `check` 校验：两处外参逐值一致；`calibrated=true` 时外参**不能全 0**；class_rules 映射正确；
  灯带/详细/legacy 话题默认关闭；stale/timeout 阈值合理；`grasp_compensation` 合法。

## 4. 标定安全闸（不会误用无效坐标）

三层防护，未标定时**不会**输出可抓取坐标：
1. `vision_bridge_node`：**全零外参 = 未标定**，不产出 robot/arm 坐标（连 `_raw` 都安全）。
   启动日志打印 `extrinsic calibration: robot_camera=... arm1_robot=... arm2_robot=...`，
   未标定显示 `IDENTITY`。
2. `calibration_guard_node`：`*_calibrated=false` → 强制 `valid_*_xyz=false`、清零 control、
   丢弃未标定的 control 选择（`drop_uncalibrated_control_selection`）。
3. 选择器 `matches_request`：GRASP 请求 `require_control_xyz=true` 时，只选 `valid_control_xyz=true`。

## 5. CENTER / GRASP / QR / assembly 请求

用 `vision_request_demo.py` 调试（`--repeat-sec` 要快于 `request_timeout_sec=0.5`）：
```bash
# CENTER：可见即可，用 u/v + align_err，不要 3D
ros2 run techx_vision_bridge vision_request_demo.py --name head_center
ros2 run techx_vision_bridge vision_request_demo.py --name kfs_red_r2_true_center
# GRASP：强制 require_control_xyz=true，只用 valid_control_xyz=true
ros2 run techx_vision_bridge vision_request_demo.py --name head_fist_grasp
ros2 run techx_vision_bridge vision_request_demo.py --name kfs_red_r2_true_grasp
# QR 对齐 / 灯带事件：不要 3D
ros2 run techx_vision_bridge vision_request_demo.py --name qr_align
ros2 run techx_vision_bridge vision_request_demo.py --name assembly_success
```
预设里 `*_center / qr* / assembly*` 的 `require_control_xyz=false`，`*_grasp` 为 `true`。
决策包必须照此区分：CENTER 不强制 3D，GRASP 必须强制。

## 6. 抓取补偿 dx/dy/dz（可选）

`calibration_guard_node` 的 `grasp_compensation`，只作用于 `control_x/y/z`，不动 camera/robot/arm：
```yaml
# config/vision_bridge.yaml -> calibration_guard_node
# 格式 "class_or_range:dx:dy:dz"（米），默认 [""] 不补偿
grasp_compensation: ["100-102:0.0:0.0:-0.012", "0-5:0.0:0.008:0.0"]
```
- 仅在 `valid_control_xyz=true` 时施加；未标定/无效目标不补偿。
- `selection_debug` CSV 同时记录补偿前(`raw_control_*`)与补偿后(`best_control_*`)，便于核对。
- `check_vision_bridge_config.py` 校验补偿条目格式，并对非抓取类(0-5/100-102 之外)或 >0.10m 告警。

## 7. selected 没输出时看原因

`/tmp/techx_gmk_selected_debug.csv` 的 `reason` 列：
- `NO_REQUEST / NO_FRAME / FRAME_STALE / REQUEST_STALE`：链路/时序问题。
- `NO_MATCH_CLASS / NO_MATCH_TYPE / NO_MATCH_ZONE / NO_MATCH_COLOR`：过滤条件不匹配。
- `LOW_CONFIDENCE`：置信度低于请求阈值。
- `UNCALIBRATED`：目标在 arm/robot 帧但未标定 → 被安全闸拦下。
- `NO_VALID_CONTROL_XYZ`：要 3D 但该目标没有有效 control 坐标（无深度/质量门）。

掉包/断连保护：`watchdog_timeout_sec`、`request_timeout_sec`、`max_frame_age_sec` →
帧/请求过旧会报 `FRAME_STALE/REQUEST_STALE`，**不会拿旧坐标继续抓**。

## 8. 启动命令

```bash
cd <ros2_ws>
python3 src/techx_vision_bridge/tools/check_vision_bridge_config.py   # 赛前自检
bash scripts/field_start_gmk.sh        # 配网 + colcon build + launch
# 或手动：
colcon build --packages-select techx_vision_bridge && source install/setup.bash
ros2 launch techx_vision_bridge vision_bridge.launch.py
```
关注启动日志：`vision bridge ready ...`、`extrinsic calibration: ...`、`first UDP frame received`。

## 9. 备份

- `config/vision_bridge.yaml`（含已写入外参 + flag + 补偿）。
- ROS2 工作区代码（分支 `fix/field-calibration-safety` + commit）。
- `apply_calibration_yaml.py` 生成的 `*.bak.*` 备份；标定来源 YAML/report/residuals（从 Jetson 拷来）。
- `/tmp/techx_gmk_selected_debug.csv`（赛后复盘）。

## 10. 启动顺序（当天）

先 GMK（等 `calibrated` + `first UDP frame received`）→ 再 Jetson → 决策包用 guarded 话题，
按 CENTER→GRASP 顺序发请求。详见视觉仓库 `docs/FIELD_OPERATIONS.md` 第 10 节。
