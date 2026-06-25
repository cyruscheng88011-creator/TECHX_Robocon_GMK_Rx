#!/usr/bin/env python3
# Usage: ros2 launch techx_vision_bridge vision_bridge.launch.py
# Starts:
#   vision_bridge_node       Jetson UDP V2 -> raw ROS 2 topics
#   calibration_guard_node   raw topics -> guarded canonical topics
#   selection_debug_node     request/selection reason CSV -> /tmp

import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    pkg_share = get_package_share_directory("techx_vision_bridge")
    config_path = os.path.join(pkg_share, "config", "vision_bridge.yaml")

    bridge_node = Node(
        package="techx_vision_bridge",
        executable="vision_bridge_node",
        name="vision_bridge_node",
        output="screen",
        parameters=[config_path],
    )

    calibration_guard_node = Node(
        package="techx_vision_bridge",
        executable="calibration_guard_node.py",
        name="calibration_guard_node",
        output="screen",
        parameters=[config_path],
    )

    selection_debug_node = Node(
        package="techx_vision_bridge",
        executable="selection_debug_node.py",
        name="selection_debug_node",
        output="screen",
        parameters=[config_path],
    )

    return LaunchDescription([bridge_node, calibration_guard_node, selection_debug_node])
