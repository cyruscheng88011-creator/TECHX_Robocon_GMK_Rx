#!/usr/bin/env python3
"""Guard canonical vision outputs until field extrinsics are explicitly calibrated.

vision_frame_bridge_node can decode Jetson UDP and compute multiple coordinate
frames. During field bring-up the extrinsic parameters often stay at zero until
hand-eye calibration is finished. This node prevents those placeholder transforms
from being consumed as real robot/arm coordinates.

Typical launch wiring:
  bridge publishes /techx/vision/frame_raw and /techx/vision/selected_raw
  calibration_guard_node publishes canonical /techx/vision/frame and
  /techx/vision/selected after sanitizing validity flags.
"""

from __future__ import annotations

import copy
from typing import Optional

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy, HistoryPolicy

from techx_vision_bridge.msg import VisionFrame, VisionObject, VisionSelection


FRAME_CAMERA_LINK = 1
FRAME_ROBOT_BASE = 2
FRAME_ARM1_BASE = 3
FRAME_ARM2_BASE = 4


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
        self.allow_camera_link_control = bool(
            self.get_parameter("allow_camera_link_control_when_uncalibrated").value
        )

        self.frame_pub = self.create_publisher(
            VisionFrame, str(self.get_parameter("output_frame_topic").value), qos
        )
        self.selection_pub = self.create_publisher(
            VisionSelection, str(self.get_parameter("output_selected_topic").value), qos
        )

        self.frame_sub = self.create_subscription(
            VisionFrame,
            str(self.get_parameter("input_frame_topic").value),
            self.on_frame,
            qos,
        )
        self.selection_sub = self.create_subscription(
            VisionSelection,
            str(self.get_parameter("input_selected_topic").value),
            self.on_selection,
            qos,
        )

        self.get_logger().info(
            "calibration guard ready robot_camera=%s arm1_robot=%s arm2_robot=%s"
            % (
                self.robot_camera_calibrated,
                self.arm1_robot_calibrated,
                self.arm2_robot_calibrated,
            )
        )
        if not self.robot_camera_calibrated:
            self.get_logger().warn(
                "T_robot_camera is not marked calibrated: robot_base/arm_base/control XYZ will be invalid unless camera_link control is requested."
            )
        if not self.arm1_robot_calibrated:
            self.get_logger().warn("T_arm1_robot is not marked calibrated: arm1_base XYZ will be invalid.")
        if not self.arm2_robot_calibrated:
            self.get_logger().warn("T_arm2_robot is not marked calibrated: arm2_base XYZ will be invalid.")

    def sanitize_object(self, obj: VisionObject) -> VisionObject:
        out = copy.deepcopy(obj)

        # camera_link is raw Jetson RGB-D output. Keep it as-is.
        robot_ok = bool(out.valid_xyz and self.robot_camera_calibrated)
        arm1_ok = bool(robot_ok and self.arm1_robot_calibrated)
        arm2_ok = bool(robot_ok and self.arm2_robot_calibrated)

        if not robot_ok:
            out.valid_robot_xyz = False
            out.robot_x = 0.0
            out.robot_y = 0.0
            out.robot_z = 0.0
        else:
            out.valid_robot_xyz = bool(out.valid_robot_xyz)

        if not arm1_ok:
            out.valid_arm1_xyz = False
            out.arm1_x = 0.0
            out.arm1_y = 0.0
            out.arm1_z = 0.0
        else:
            out.valid_arm1_xyz = bool(out.valid_arm1_xyz)

        if not arm2_ok:
            out.valid_arm2_xyz = False
            out.arm2_x = 0.0
            out.arm2_y = 0.0
            out.arm2_z = 0.0
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
            out.control_x = 0.0
            out.control_y = 0.0
            out.control_z = 0.0

        return out

    def on_frame(self, msg: VisionFrame) -> None:
        out = copy.deepcopy(msg)
        out.targets = [self.sanitize_object(t) for t in msg.targets]
        self.frame_pub.publish(out)

    def on_selection(self, msg: VisionSelection) -> None:
        out = copy.deepcopy(msg)
        if msg.has_match:
            out.target = self.sanitize_object(msg.target)
            # Keep STATUS_OK for 2D/event-only targets. If the request requires control XYZ,
            # the bridge should already have rejected invalid raw selections; this guard only
            # prevents calibrated-frame coordinates from being falsely consumed.
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
