#!/usr/bin/env python3
"""Compatibility launcher for the GMK mock Jetson sender.

Prefer using:
  python3 src/techx_vision_bridge/tools/mock_jetson_sender.py --mode mixed
or, after colcon build:
  ros2 run techx_vision_bridge mock_jetson_sender.py --mode mixed
"""

from pathlib import Path
import runpy

SCRIPT = Path(__file__).resolve().parent / "src" / "techx_vision_bridge" / "tools" / "mock_jetson_sender.py"
runpy.run_path(str(SCRIPT), run_name="__main__")
