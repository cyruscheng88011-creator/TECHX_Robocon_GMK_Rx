#!/usr/bin/env python3
"""Write selector diagnostics to CSV without changing VisionSelection.msg."""

from __future__ import annotations

import csv
import os
import time
from typing import Optional, Tuple

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy, HistoryPolicy

from techx_vision_bridge.msg import VisionFrame, VisionObject, VisionRequest, VisionSelection


FIELDS = [
    "time", "frame_seq", "request_seq", "status", "reason", "has_match",
    "request_class_id", "request_type", "request_zone", "require_xyz",
    "target_count", "best_class_id", "best_conf", "best_valid_xyz",
    "best_valid_control_xyz", "best_control_frame", "frame_age_sec",
]

STATUS_NAMES = {
    VisionSelection.STATUS_OK: "OK",
    VisionSelection.STATUS_NO_REQUEST: "NO_REQUEST",
    VisionSelection.STATUS_NO_FRAME: "NO_FRAME",
    VisionSelection.STATUS_NO_MATCH: "NO_MATCH",
    VisionSelection.STATUS_FRAME_STALE: "FRAME_STALE",
    VisionSelection.STATUS_REQUEST_STALE: "REQUEST_STALE",
}


class SelectionDebugNode(Node):
    def __init__(self) -> None:
        super().__init__("selection_debug_node")
        self.declare_parameter("frame_raw_topic", "/techx/vision/frame_raw")
        self.declare_parameter("request_topic", "/techx/vision/request")
        self.declare_parameter("selected_raw_topic", "/techx/vision/selected_raw")
        self.declare_parameter("selected_topic", "/techx/vision/selected")
        self.declare_parameter("csv_path", "/tmp/techx_gmk_selected_debug.csv")
        self.declare_parameter("reliable_qos", True)
        self.declare_parameter("qos_depth", 10)

        reliable = bool(self.get_parameter("reliable_qos").value)
        qos_depth = max(1, int(self.get_parameter("qos_depth").value))
        qos = QoSProfile(
            history=HistoryPolicy.KEEP_LAST,
            depth=qos_depth,
            reliability=ReliabilityPolicy.RELIABLE if reliable else ReliabilityPolicy.BEST_EFFORT,
            durability=DurabilityPolicy.VOLATILE,
        )

        self.latest_frame: Optional[VisionFrame] = None
        self.latest_request: Optional[VisionRequest] = None
        self.latest_raw_selection: Optional[VisionSelection] = None
        self.latest_guarded_selection: Optional[VisionSelection] = None
        self.frame_recv_time = 0.0
        self.request_recv_time = 0.0
        self.csv_path = str(self.get_parameter("csv_path").value)
        os.makedirs(os.path.dirname(self.csv_path) or ".", exist_ok=True)
        self.fh = open(self.csv_path, "a", newline="", encoding="utf-8")
        self.writer = csv.DictWriter(self.fh, fieldnames=FIELDS)
        if self.fh.tell() == 0:
            self.writer.writeheader()
            self.fh.flush()

        self.frame_sub = self.create_subscription(VisionFrame, str(self.get_parameter("frame_raw_topic").value), self.on_frame, qos)
        self.request_sub = self.create_subscription(VisionRequest, str(self.get_parameter("request_topic").value), self.on_request, qos)
        self.raw_sel_sub = self.create_subscription(VisionSelection, str(self.get_parameter("selected_raw_topic").value), self.on_selected_raw, qos)
        self.sel_sub = self.create_subscription(VisionSelection, str(self.get_parameter("selected_topic").value), self.on_selected, qos)
        self.get_logger().info("selection debug CSV: %s" % self.csv_path)

    def on_frame(self, msg: VisionFrame) -> None:
        self.latest_frame = msg
        self.frame_recv_time = time.time()

    def on_request(self, msg: VisionRequest) -> None:
        self.latest_request = msg
        self.request_recv_time = time.time()

    def on_selected_raw(self, msg: VisionSelection) -> None:
        self.latest_raw_selection = msg
        self.write_row(msg, guarded=False)

    def on_selected(self, msg: VisionSelection) -> None:
        self.latest_guarded_selection = msg
        self.write_row(msg, guarded=True)

    def write_row(self, sel: VisionSelection, guarded: bool) -> None:
        req = self.latest_request
        frame = self.latest_frame
        best, reason = self.explain(sel, guarded)
        row = {
            "time": time.time(),
            "frame_seq": int(sel.frame_seq or (frame.seq if frame else 0)),
            "request_seq": int(sel.request_seq or (req.request_seq if req else 0)),
            "status": STATUS_NAMES.get(sel.status, str(int(sel.status))),
            "reason": reason,
            "has_match": int(bool(sel.has_match)),
            "request_class_id": int(req.class_id) if req else 0,
            "request_type": int(req.target_type) if req else 0,
            "request_zone": int(req.zone_id) if req else 0,
            "require_xyz": int(bool(req.require_control_xyz)) if req else 0,
            "target_count": int(frame.target_count) if frame else 0,
            "best_class_id": int(best.class_id) if best else -1,
            "best_conf": float(best.confidence) if best else 0.0,
            "best_valid_xyz": int(bool(best.valid_xyz)) if best else 0,
            "best_valid_control_xyz": int(bool(best.valid_control_xyz)) if best else 0,
            "best_control_frame": int(best.control_frame) if best else 0,
            "frame_age_sec": float(sel.frame_age_sec),
        }
        try:
            self.writer.writerow(row)
            self.fh.flush()
        except Exception as exc:
            self.get_logger().warn("selection debug CSV write failed: %s" % exc)

    def explain(self, sel: VisionSelection, guarded: bool) -> Tuple[Optional[VisionObject], str]:
        if guarded and self.latest_raw_selection and self.latest_raw_selection.has_match and not sel.has_match:
            raw_target = self.latest_raw_selection.target
            if raw_target.control_frame != VisionObject.FRAME_CAMERA_LINK and not raw_target.valid_control_xyz:
                return raw_target, "UNCALIBRATED"
            return raw_target, "BLOCKED_AFTER_SELECTOR_OR_GUARD"
        if sel.status == VisionSelection.STATUS_OK and sel.has_match:
            return sel.target, "OK"
        if sel.status == VisionSelection.STATUS_NO_REQUEST:
            return None, "NO_REQUEST"
        if sel.status == VisionSelection.STATUS_NO_FRAME:
            return None, "NO_FRAME"
        if sel.status == VisionSelection.STATUS_FRAME_STALE:
            return self.best_any(), "FRAME_STALE"
        if sel.status == VisionSelection.STATUS_REQUEST_STALE:
            return self.best_any(), "REQUEST_STALE"
        return self.explain_no_match()

    def explain_no_match(self) -> Tuple[Optional[VisionObject], str]:
        req = self.latest_request
        frame = self.latest_frame
        if req is None:
            return None, "NO_REQUEST"
        if frame is None:
            return None, "NO_FRAME"
        targets = list(frame.targets)
        if not targets:
            return None, "NO_TARGETS"
        best = max(targets, key=lambda t: float(t.confidence))
        candidates = targets
        if req.use_class_id:
            candidates = [t for t in candidates if t.class_id == req.class_id]
            if not candidates:
                return best, "NO_MATCH_CLASS"
        if req.target_type != VisionRequest.TYPE_ANY:
            candidates = [t for t in candidates if t.target_type == req.target_type]
            if not candidates:
                return best, "NO_MATCH_TYPE"
        if req.zone_id != VisionRequest.ZONE_ANY:
            candidates = [t for t in candidates if t.zone_id == req.zone_id]
            if not candidates:
                return best, "NO_MATCH_ZONE"
        if req.use_color:
            candidates = [t for t in candidates if t.color == req.color]
            if not candidates:
                return best, "NO_MATCH_COLOR"
        if req.min_confidence > 0.0:
            best_after_id = max(candidates, key=lambda t: float(t.confidence)) if candidates else best
            candidates = [t for t in candidates if t.confidence >= req.min_confidence]
            if not candidates:
                return best_after_id, "LOW_CONFIDENCE"
        if req.require_control_xyz:
            best_after_conf = max(candidates, key=lambda t: float(t.confidence)) if candidates else best
            candidates = [t for t in candidates if t.valid_control_xyz]
            if not candidates:
                if best_after_conf.control_frame != VisionObject.FRAME_CAMERA_LINK:
                    return best_after_conf, "UNCALIBRATED"
                return best_after_conf, "NO_VALID_CONTROL_XYZ"
        if candidates:
            return max(candidates, key=lambda t: float(t.priority)), "BLOCKED_AFTER_SELECTOR_OR_GUARD"
        return best, "NO_TARGETS"

    def best_any(self) -> Optional[VisionObject]:
        if not self.latest_frame or not self.latest_frame.targets:
            return None
        return max(self.latest_frame.targets, key=lambda t: float(t.confidence))

    def destroy_node(self) -> bool:
        try:
            self.fh.close()
        except Exception:
            pass
        return super().destroy_node()


def main(args=None) -> None:
    rclpy.init(args=args)
    node = SelectionDebugNode()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
