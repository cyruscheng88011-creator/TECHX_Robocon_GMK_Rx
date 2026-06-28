#!/usr/bin/env python3
"""Static checker for the GMK techx_vision_bridge YAML config."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import List, Optional, Tuple

REQUIRED_CLASS_IDS = {
    0: "kfs_red_r1",
    1: "kfs_red_r2_fake",
    2: "kfs_red_r2_true",
    3: "kfs_blue_r1",
    4: "kfs_blue_r2_fake",
    5: "kfs_blue_r2_true",
    100: "weapon_head_fist",
    101: "weapon_head_palm",
    102: "weapon_head_spear",
    150: "r1_assembly_light_red",
    151: "r1_assembly_light_blue",
    152: "assembly_success",
    200: "qr_code",
}

EXPECTED_RULES = {
    0: (2, 2, 4),      # KFS -> arm2_base
    100: (1, 1, 3),    # weapon head -> arm1_base
    150: (10, 10, 1),  # assembly event -> camera_link
    200: (3, 3, 2),    # QR -> robot_base
}

RULE_RE = re.compile(r'"([^":]+):(\d+):(\d+):(\d+):([-+0-9.]+)"')
LIST_RE_TMPL = r"^\s*{key}:\s*\[([^\]]*)\]"


def read_rules(text: str) -> List[Tuple[int, int, int, int, int, float]]:
    rules = []
    for match in RULE_RE.finditer(text):
        range_s, zone_s, type_s, frame_s, bias_s = match.groups()
        if "-" in range_s:
            lo_s, hi_s = range_s.split("-", 1)
            lo, hi = int(lo_s), int(hi_s)
        else:
            lo = hi = int(range_s)
        if hi < lo:
            lo, hi = hi, lo
        rules.append((lo, hi, int(zone_s), int(type_s), int(frame_s), float(bias_s)))
    return rules


def matching_rule(rules: List[Tuple[int, int, int, int, int, float]], cid: int) -> Optional[Tuple[int, int, int, int, int, float]]:
    for rule in rules:
        lo, hi, *_ = rule
        if lo <= cid <= hi:
            return rule
    return None


def covered(rules: List[Tuple[int, int, int, int, int, float]], cid: int) -> bool:
    return matching_rule(rules, cid) is not None


def scalar_value(text: str, key: str) -> Optional[str]:
    m = re.search(rf"^\s*{re.escape(key)}:\s*([^#\n]+)", text, re.M)
    return m.group(1).strip() if m else None


def scalar_values(text: str, key: str) -> List[str]:
    return [m.group(1).strip() for m in re.finditer(rf"^\s*{re.escape(key)}:\s*([^#\n]+)", text, re.M)]


def bool_value(text: str, key: str) -> Optional[bool]:
    value = scalar_value(text, key)
    if value is None:
        return None
    return value.strip().lower() in {"true", "1", "yes", "on"}


def list_value(text: str, key: str) -> Optional[List[float]]:
    values = list_values(text, key)
    return values[0] if values else None


def list_values(text: str, key: str) -> List[List[float]]:
    out_all: List[List[float]] = []
    for m in re.finditer(LIST_RE_TMPL.format(key=re.escape(key)), text, re.M):
        out = []
        ok = True
        for item in m.group(1).split(","):
            item = item.strip()
            if not item:
                continue
            try:
                out.append(float(item))
            except ValueError:
                ok = False
                break
        if ok:
            out_all.append(out)
    return out_all


def all_zero(values: Optional[List[float]]) -> bool:
    return values is not None and len(values) == 6 and all(abs(v) < 1e-9 for v in values)


def same_list(a: List[float], b: List[float], eps: float = 1e-9) -> bool:
    return len(a) == len(b) == 6 and all(abs(x - y) <= eps for x, y in zip(a, b))


def require_key(text: str, key: str) -> bool:
    if scalar_value(text, key) is None:
        print(f"[ERROR] missing {key}")
        return False
    return True


def parse_float(text: str, key: str) -> Optional[float]:
    value = scalar_value(text, key)
    if value is None:
        return None
    try:
        return float(value)
    except ValueError:
        print(f"[ERROR] {key} is not a float: {value}")
        return None


def check_calibration_flag(text: str, flag_key: str, transform_key: str) -> int:
    flag = bool_value(text, flag_key)
    values_all = list_values(text, transform_key)
    errors = 0
    if flag is None:
        print(f"[ERROR] missing {flag_key}")
        return 1
    if len(values_all) != 2:
        print(f"[ERROR] expected {transform_key} in both vision_bridge_node and calibration_guard_node; found {len(values_all)}")
        return 1
    if any(len(v) != 6 for v in values_all):
        print(f"[ERROR] invalid {transform_key}; expected 6 values")
        return 1
    if not same_list(values_all[0], values_all[1]):
        print(f"[ERROR] {transform_key} differs between vision_bridge_node and calibration_guard_node")
        errors += 1
    values = values_all[0]
    if flag and all_zero(values):
        print(f"[ERROR] {flag_key}=true but {transform_key} is all zeros")
        errors += 1
    elif not flag and all_zero(values):
        print(f"[WARN] {transform_key} is still all zeros; keep {flag_key}=false until field calibration is done")
    elif not flag and not all_zero(values):
        print(f"[WARN] {transform_key} is non-zero but {flag_key}=false; guarded /selected will block this frame")
    else:
        print(f"[OK] {flag_key}=true and {transform_key} is populated consistently")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Check GMK vision bridge config without ROS/hardware")
    parser.add_argument("--config", default="src/techx_vision_bridge/config/vision_bridge.yaml")
    args = parser.parse_args()

    text = Path(args.config).read_text(encoding="utf-8")
    errors = 0

    for top in ("vision_bridge_node:", "calibration_guard_node:"):
        if top not in text:
            print(f"[ERROR] missing top-level {top.rstrip(':')}")
            errors += 1
    if "selection_debug_node:" not in text:
        print("[WARN] missing top-level selection_debug_node; launch defaults will still run but CSV path cannot be configured")

    for key in (
        "udp_bind_addr", "udp_port", "frame_topic_name", "request_topic_name", "selected_topic_name",
        "enable_request_selector", "watchdog_timeout_sec", "fatal_no_udp_timeout_sec",
        "request_timeout_sec", "default_max_frame_age_sec",
        "input_frame_topic", "output_frame_topic", "input_selected_topic", "output_selected_topic",
    ):
        if not require_key(text, key):
            errors += 1

    rules = read_rules(text)
    if not rules:
        print("[ERROR] no valid class_rules found")
        errors += 1
    else:
        print("class_rules:")
        for lo, hi, zone, typ, frame, bias in rules:
            print(f"  {lo}-{hi}: zone={zone} type={typ} frame={frame} bias={bias}")

    for cid, name in REQUIRED_CLASS_IDS.items():
        if not covered(rules, cid):
            print(f"[ERROR] class_id {cid} ({name}) is not covered by class_rules")
            errors += 1

    for cid, expected in EXPECTED_RULES.items():
        rule = matching_rule(rules, cid)
        if rule is None:
            continue
        _lo, _hi, zone, typ, frame, _bias = rule
        if (zone, typ, frame) != expected:
            print(f"[ERROR] class_id {cid} maps to zone/type/frame {(zone, typ, frame)} but expected {expected}")
            errors += 1

    for key in ("publish_detail_topic", "publish_legacy_topic", "accept_legacy"):
        value = scalar_value(text, key)
        if value is None:
            print(f"[ERROR] missing {key}")
            errors += 1
        elif value.lower() != "false":
            print(f"[WARN] {key} is {value}; canonical competition path should keep legacy/detail topics off by default")

    if scalar_value(text, "enable_request_selector") not in ("true", "True"):
        print("[WARN] enable_request_selector is not true; /request -> /selected will be disabled")

    request_timeout = parse_float(text, "request_timeout_sec")
    if request_timeout is None:
        errors += 1
    elif request_timeout < 0.3:
        print("[WARN] request_timeout_sec is very short; decision code must publish faster than this")
    else:
        print(f"request_timeout_sec: {request_timeout:.2f}s")

    frame_age = parse_float(text, "default_max_frame_age_sec")
    if frame_age is None:
        errors += 1
    elif frame_age > 0.5:
        print("[WARN] default_max_frame_age_sec is high; stale targets may remain selectable")
    else:
        print(f"default_max_frame_age_sec: {frame_age:.2f}s")

    fatal_timeout = parse_float(text, "fatal_no_udp_timeout_sec")
    if fatal_timeout is None:
        errors += 1
    elif fatal_timeout == 0.0:
        print("[WARN] fatal_no_udp_timeout_sec is disabled; long no-data runs will not auto-terminate")
    elif fatal_timeout < 60.0:
        print("[WARN] fatal_no_udp_timeout_sec is very short; this may kill the bridge during normal startup")
    else:
        print(f"fatal_no_udp_timeout_sec: {fatal_timeout:.1f}s")

    errors += check_calibration_flag(text, "robot_camera_calibrated", "T_robot_camera_xyz_rpy")
    errors += check_calibration_flag(text, "arm1_robot_calibrated", "T_arm1_robot_xyz_rpy")
    errors += check_calibration_flag(text, "arm2_robot_calibrated", "T_arm2_robot_xyz_rpy")

    if errors:
        print(f"FAILED: {errors} error(s)")
        return 1
    print("OK: GMK vision bridge config matches the field communication and calibration contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
