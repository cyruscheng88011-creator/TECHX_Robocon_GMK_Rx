#!/usr/bin/env python3
"""Guard canonical vision outputs until field extrinsics are explicitly calibrated."""

from __future__ import annotations

import copy
from typing import List, Optional, Tuple

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy, HistoryPolicy

from techx_vision_bridge.msg import VisionFrame, VisionObject, VisionSelection


FRAME_CAMERA_LINK = 1
FRAME_ROBOT_BASE = 2
FRAME_ARM1_BASE = 3
FRAME_ARM2_BASE = 4


# (min_class_id, max_class_id, dx, dy, dz) in the object's control frame, meters.
CompRule = Tuple[int, int, float, float, float]


def parse_grasp_compensation(raw: List[str]) -> List[CompRule]:
    """Parse 'class_or_range:dx:dy:dz' strings into compensation rules.

    Bad entries are skipped (caller logs the count) so one typo cannot crash the
    safety node. dx/dy/dz are added to control_x/y/z only; camera/robot/arm
    coordinates are never modified.
    """
    rules: List[CompRule] = []
    for line in raw or []:
        parts = str(line).split(":")
        if len(parts) != 4:
            continue
        range_s = parts[0].strip()
        try:
            if "-" in range_s:
                lo_s, hi_s = range_s.split("-", 1)
                lo, hi = int(lo_s), int(hi_s)
            else:
                lo = hi = int(range_s)
            if hi < lo:
                lo, hi = hi, lo
            dx, dy, dz = float(parts[1]), float(parts[2]), float(parts[3])
        except (TypeError, ValueError):
            continue
        rules.append((lo, hi, dx, dy, dz))
    return rules


def lookup_compensation(rules: List[CompRule], class_id: int) -> Optional[Tuple[float, float, float]]:
    cid = int(class_id)
    for lo, hi, dx, dy, dz in rules:
        if lo <= cid <= hi:
            return dx, dy, dz
    return None


class CalibrationGuardNode(Node):
    def __init__(self) -> None:
        super().__init__("calibration_guard_node")

        self.declare_parameter("input_frame_topic", "/techx/vision/frame_raw")
        self.declare_parameter("output_frame_topic", "/techx/vision/frame")
        self.declare_parameter("input_selected_topic", "/techx/vision/selected_raw")
        self.declare_parameter("output_selected_topic", "/techx/vision/selected")
        self.declare_parameter("robot_camera_calibrated", False)
        self.declare_parameter("arm1_robot_calibrated", False)
        self.declare_parameter("arm2_robot_calibrated", False)
        self.declare_parameter("allow_camera_link_control_when_uncalibrated", True)
        self.declare_parameter("drop_uncalibrated_control_selection", True)
        # Lightweight per-class grasp offset applied to control_x/y/z only.
        # Format: ["class_or_range:dx:dy:dz", ...] meters, e.g. "100-102:0.0:0.0:-0.012".
        self.declare_parameter("grasp_compensation", [""])
        self.declare_parameter("reliable_qos", True)
        self.declare_parameter("qos_depth", 5)

        reliable = bool(self.get_parameter("reliable_qos").value)
        qos_depth = max(1, int(self.get_parameter("qos_depth").value))
        qos = QoSProfile(
            history=HistoryPolicy.KEEP_LAST,
            depth=qos_depth,
            reliability=ReliabilityPolicy.RELIABLE if reliable else ReliabilityPolicy.BEST_EFFORT,
            durability=DurabilityPolicy.VOLATILE,
        )

        self.robot_camera_calibrated = bool(self.get_parameter("robot_camera_calibrated").value)
        self.arm1_robot_calibrated = bool(self.get_parameter("arm1_robot_calibrated").value)
        self.arm2_robot_calibrated = bool(self.get_parameter("arm2_robot_calibrated").value)
        self.allow_camera_link_control = bool(self.get_parameter("allow_camera_link_control_when_uncalibrated").value)
        self.drop_uncalibrated_control_selection = bool(self.get_parameter("drop_uncalibrated_control_selection").value)

        raw_comp = list(self.get_parameter("grasp_compensation").value or [])
        raw_comp = [s for s in raw_comp if str(s).strip()]
        self.grasp_compensation = parse_grasp_compensation(raw_comp)
        if len(self.grasp_compensation) != len(raw_comp):
            self.get_logger().warn(
                "grasp_compensation: %d/%d entries parsed; check 'class_or_range:dx:dy:dz' format"
                % (len(self.grasp_compensation), len(raw_comp))
            )

        self.frame_pub = self.create_publisher(VisionFrame, str(self.get_parameter("output_frame_topic").value), qos)
        self.selection_pub = self.create_publisher(VisionSelection, str(self.get_parameter("output_selected_topic").value), qos)
        self.frame_sub = self.create_subscription(VisionFrame, str(self.get_parameter("input_frame_topic").value), self.on_frame, qos)
        self.selection_sub = self.create_subscription(VisionSelection, str(self.get_parameter("input_selected_topic").value), self.on_selection, qos)

        self.get_logger().info(
            "calibration guard ready robot_camera=%s arm1_robot=%s arm2_robot=%s drop_blocked=%s"
            % (
                self.robot_camera_calibrated,
                self.arm1_robot_calibrated,
                self.arm2_robot_calibrated,
                self.drop_uncalibrated_control_selection,
            )
        )
        if not self.robot_camera_calibrated:
            self.get_logger().warn("robot_base/arm_base coordinates are blocked until T_robot_camera is marked calibrated")
        if not self.arm1_robot_calibrated:
            self.get_logger().warn("arm1_base coordinates are blocked until T_arm1_robot is marked calibrated")
        if not self.arm2_robot_calibrated:
            self.get_logger().warn("arm2_base coordinates are blocked until T_arm2_robot is marked calibrated")
        if self.grasp_compensation:
            table = ", ".join(
                "%d-%d:(%.4f,%.4f,%.4f)" % (lo, hi, dx, dy, dz)
                for lo, hi, dx, dy, dz in self.grasp_compensation
            )
            self.get_logger().info("grasp_compensation active (control_x/y/z only): %s" % table)
        else:
            self.get_logger().info("grasp_compensation: none configured; control_x/y/z passed through unchanged")

    def sanitize_object(self, obj: VisionObject) -> VisionObject:
        out = copy.deepcopy(obj)
        robot_ok = bool(out.valid_xyz and self.robot_camera_calibrated)
        arm1_ok = bool(robot_ok and self.arm1_robot_calibrated)
        arm2_ok = bool(robot_ok and self.arm2_robot_calibrated)

        if not robot_ok:
            out.valid_robot_xyz = False
            out.robot_x = out.robot_y = out.robot_z = 0.0
        else:
            out.valid_robot_xyz = bool(out.valid_robot_xyz)

        if not arm1_ok:
            out.valid_arm1_xyz = False
            out.arm1_x = out.arm1_y = out.arm1_z = 0.0
        else:
            out.valid_arm1_xyz = bool(out.valid_arm1_xyz)

        if not arm2_ok:
            out.valid_arm2_xyz = False
            out.arm2_x = out.arm2_y = out.arm2_z = 0.0
        else:
            out.valid_arm2_xyz = bool(out.valid_arm2_xyz)

        if out.control_frame == FRAME_CAMERA_LINK:
            out.valid_control_xyz = bool(out.valid_xyz and self.allow_camera_link_control)
            out.control_x = out.x if out.valid_control_xyz else 0.0
            out.control_y = out.y if out.valid_control_xyz else 0.0
            out.control_z = out.z if out.valid_control_xyz else 0.0
        elif out.control_frame == FRAME_ROBOT_BASE:
            out.valid_control_xyz = bool(out.valid_robot_xyz)
            out.control_x = out.robot_x if out.valid_control_xyz else 0.0
            out.control_y = out.robot_y if out.valid_control_xyz else 0.0
            out.control_z = out.robot_z if out.valid_control_xyz else 0.0
        elif out.control_frame == FRAME_ARM1_BASE:
            out.valid_control_xyz = bool(out.valid_arm1_xyz)
            out.control_x = out.arm1_x if out.valid_control_xyz else 0.0
            out.control_y = out.arm1_y if out.valid_control_xyz else 0.0
            out.control_z = out.arm1_z if out.valid_control_xyz else 0.0
        elif out.control_frame == FRAME_ARM2_BASE:
            out.valid_control_xyz = bool(out.valid_arm2_xyz)
            out.control_x = out.arm2_x if out.valid_control_xyz else 0.0
            out.control_y = out.arm2_y if out.valid_control_xyz else 0.0
            out.control_z = out.arm2_z if out.valid_control_xyz else 0.0
        else:
            out.valid_control_xyz = False
            out.control_x = out.control_y = out.control_z = 0.0

        self._apply_grasp_compensation(out)
        return out

    def _apply_grasp_compensation(self, out: VisionObject) -> None:
        """Add a per-class offset to control_x/y/z only (camera/robot/arm untouched).

        Applied after the calibration gate, so an uncalibrated or invalid target
        (valid_control_xyz=False, control zeroed) never receives a phantom offset.
        """
        if not self.grasp_compensation or not out.valid_control_xyz:
            return
        comp = lookup_compensation(self.grasp_compensation, out.class_id)
        if comp is None:
            return
        dx, dy, dz = comp
        out.control_x = float(out.control_x) + dx
        out.control_y = float(out.control_y) + dy
        out.control_z = float(out.control_z) + dz

    def _should_drop_selection(self, target: VisionObject) -> bool:
        if not self.drop_uncalibrated_control_selection:
            return False
        if target.control_frame == FRAME_CAMERA_LINK:
            return False
        return not bool(target.valid_control_xyz)

    def on_frame(self, msg: VisionFrame) -> None:
        out = copy.deepcopy(msg)
        out.targets = [self.sanitize_object(t) for t in msg.targets]
        self.frame_pub.publish(out)

    def on_selection(self, msg: VisionSelection) -> None:
        out = copy.deepcopy(msg)
        if msg.has_match:
            out.target = self.sanitize_object(msg.target)
            if self._should_drop_selection(out.target):
                out.has_match = False
                out.status = VisionSelection.STATUS_NO_MATCH
                out.selected_index = 255
                out.score = 0.0
        self.selection_pub.publish(out)


def main(args: Optional[list[str]] = None) -> None:
    rclpy.init(args=args)
    node = CalibrationGuardNode()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
