#!/usr/bin/env python3
"""Safely apply Jetson-exported calibration snippets to GMK vision_bridge.yaml.

The Jetson exporter writes small YAML snippets containing one or more of:
  T_robot_camera_xyz_rpy: [...]
  T_arm1_robot_xyz_rpy: [...]
  T_arm2_robot_xyz_rpy: [...]
  robot_camera_calibrated: true
  arm1_robot_calibrated: true
  arm2_robot_calibrated: true

This tool updates both parameter blocks in the GMK config so field operators do
not need to hand-copy repeated transform values.

Example:
  python3 src/techx_vision_bridge/tools/apply_calibration_yaml.py \
    --config src/techx_vision_bridge/config/vision_bridge.yaml \
    --snippet /tmp/gmk_robot_camera.yaml
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
import time
from pathlib import Path
from typing import Dict, List

TRANSFORM_KEYS = [
    "T_robot_camera_xyz_rpy",
    "T_arm1_robot_xyz_rpy",
    "T_arm2_robot_xyz_rpy",
]
FLAG_KEYS = {
    "T_robot_camera_xyz_rpy": "robot_camera_calibrated",
    "T_arm1_robot_xyz_rpy": "arm1_robot_calibrated",
    "T_arm2_robot_xyz_rpy": "arm2_robot_calibrated",
}


def parse_list(raw: str) -> List[float]:
    m = re.search(r"\[([^\]]+)\]", raw)
    if not m:
        raise ValueError(f"not a list: {raw}")
    vals = [float(x.strip()) for x in m.group(1).split(",")]
    if len(vals) != 6:
        raise ValueError(f"expected 6 values, got {len(vals)}: {raw}")
    return vals


def parse_bool(raw: str) -> bool:
    val = raw.strip().split("#", 1)[0].strip().lower()
    if val in {"true", "1", "yes"}:
        return True
    if val in {"false", "0", "no"}:
        return False
    raise ValueError(f"invalid bool: {raw}")


def load_snippet(path: Path) -> tuple[Dict[str, List[float]], Dict[str, bool]]:
    text = path.read_text(encoding="utf-8")
    transforms: Dict[str, List[float]] = {}
    flags: Dict[str, bool] = {}
    for line in text.splitlines():
        stripped = line.strip()
        for key in TRANSFORM_KEYS:
            if stripped.startswith(key + ":"):
                transforms[key] = parse_list(stripped)
        for flag in FLAG_KEYS.values():
            if stripped.startswith(flag + ":"):
                flags[flag] = parse_bool(stripped.split(":", 1)[1])
    if not transforms:
        raise ValueError(f"no T_*_xyz_rpy transform found in {path}")
    # The Jetson exporter always writes an explicit *_calibrated flag (true only
    # when the fit passed its thresholds). If a flag is missing from a hand-written
    # or legacy snippet, fail closed: keep it false so a transform can never be
    # silently advertised as calibrated. Operators can flip it by hand after review.
    for key in transforms:
        if FLAG_KEYS[key] not in flags:
            print(f"[GMK] WARN: {FLAG_KEYS[key]} absent in snippet; defaulting to false (fail-closed)")
            flags[FLAG_KEYS[key]] = False
    return transforms, flags


def format_values(vals: List[float]) -> str:
    return "[" + ", ".join(f"{float(v):.9f}" for v in vals) + "]"


def replace_line(text: str, key: str, replacement: str, expected_count: int | None = None) -> tuple[str, int]:
    pattern = re.compile(rf"^(\s*){re.escape(key)}\s*:\s*.*$", re.MULTILINE)
    count = 0

    def repl(match: re.Match) -> str:
        nonlocal count
        count += 1
        return f"{match.group(1)}{key}: {replacement}"

    new = pattern.sub(repl, text)
    if expected_count is not None and count != expected_count:
        raise ValueError(f"expected {expected_count} occurrences of {key}, found {count}")
    if count == 0:
        raise ValueError(f"key not found in config: {key}")
    return new, count


def apply(config: Path, snippet: Path, backup: bool) -> None:
    transforms, flags = load_snippet(snippet)
    text = config.read_text(encoding="utf-8")
    original = text
    for key, vals in transforms.items():
        # Each transform must exist in vision_bridge_node and calibration_guard_node.
        text, _ = replace_line(text, key, format_values(vals), expected_count=2)
    for flag, val in flags.items():
        # Flags live only in calibration_guard_node.
        text, _ = replace_line(text, flag, "true" if val else "false", expected_count=1)
    if text == original:
        print("[GMK] No changes")
        return
    if backup:
        backup_path = config.with_suffix(config.suffix + f".bak.{int(time.time())}")
        shutil.copy2(config, backup_path)
        print(f"[GMK] Backup: {backup_path}")
    config.write_text(text, encoding="utf-8")
    print(f"[GMK] Applied {snippet} -> {config}")
    for key, vals in transforms.items():
        print(f"[GMK] {key}: {format_values(vals)}")
    for flag, val in flags.items():
        print(f"[GMK] {flag}: {'true' if val else 'false'}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Apply Jetson-exported calibration YAML to GMK vision_bridge.yaml")
    parser.add_argument("--config", default="src/techx_vision_bridge/config/vision_bridge.yaml")
    parser.add_argument("--snippet", required=True, help="YAML snippet exported by Jetson tools/export_handeye_yaml.py")
    parser.add_argument("--no-backup", action="store_true")
    args = parser.parse_args()
    try:
        apply(Path(args.config), Path(args.snippet), backup=not args.no_backup)
    except Exception as exc:
        print(f"[GMK] ERROR: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
