#!/usr/bin/env python3
import argparse
import math
import socket
import struct
import time

MAGIC_LEGACY = 0x55AA
MAGIC_V2 = 0x55AB
VERSION_V2 = 2
SEND_TO_IP = "192.168.10.100"
SEND_TO_PORT = 12345


def build_crc16_table():
    table = []
    for i in range(256):
        crc = i << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
        table.append(crc)
    return table


CRC16_TABLE = build_crc16_table()


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        idx = ((crc >> 8) ^ byte) & 0xFF
        crc = ((crc << 8) & 0xFFFF) ^ CRC16_TABLE[idx]
    return crc


def build_legacy(seq: int, track_id: int, timestamp: float, x: float, y: float, z: float) -> bytes:
    payload = struct.pack("<H I d B 3f", MAGIC_LEGACY, seq & 0xFFFFFFFF, timestamp, track_id & 0xFF, x, y, z)
    return payload + struct.pack("<H", crc16_ccitt(payload))


def build_v2(seq: int, timestamp: float, count: int, invalid_depth: bool = False) -> bytes:
    count = max(0, min(16, count))
    payload = struct.pack("<H B B I d B", MAGIC_V2, VERSION_V2, 0, seq & 0xFFFFFFFF, timestamp, count)
    for tid in range(count):
        t = timestamp * 0.5 + tid
        class_id = 1 + (tid % 4)
        color = 1 if class_id in (1, 3) else 2
        conf = 0.85 - 0.03 * tid
        u = 320.0 + 80.0 * math.sin(t)
        v = 240.0 + 40.0 * math.cos(t)
        x = 0.10 * math.sin(t)
        y = 0.05 * math.cos(t)
        z = 0.0 if invalid_depth else 1.20 + 0.10 * math.sin(t * 1.7)
        payload += struct.pack("<B B B f 2f 3f", tid & 0xFF, class_id, color, conf, u, v, x, y, z)
    return payload + struct.pack("<H", crc16_ccitt(payload))


def main():
    parser = argparse.ArgumentParser(description="TECHX vision UDP sender")
    parser.add_argument("--ip", default=SEND_TO_IP)
    parser.add_argument("--port", type=int, default=SEND_TO_PORT)
    parser.add_argument("--rate", type=float, default=30.0)
    parser.add_argument("--targets", type=int, default=1)
    parser.add_argument("--mode", choices=["v2", "legacy", "both", "empty", "invalid-depth"], default="v2")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    seq = 0
    interval = 1.0 / max(1.0, args.rate)
    print(f"send to {args.ip}:{args.port} mode={args.mode} rate={args.rate}Hz")

    try:
        while True:
            ts = time.time()
            packets = []
            if args.mode in ("v2", "both", "invalid-depth"):
                packets.append(build_v2(seq, ts, args.targets, invalid_depth=(args.mode == "invalid-depth")))
                seq = (seq + 1) & 0xFFFFFFFF
            elif args.mode == "empty":
                packets.append(build_v2(seq, ts, 0))
                seq = (seq + 1) & 0xFFFFFFFF

            if args.mode in ("legacy", "both"):
                packets.append(build_legacy(seq, 0, ts, 0.1, 0.0, 1.2))
                seq = (seq + 1) & 0xFFFFFFFF

            for packet in packets:
                sock.sendto(packet, (args.ip, args.port))

            if seq % int(max(1, args.rate * 2)) == 0:
                print(f"sent seq={seq} packets={len(packets)} last_len={len(packets[-1])}")
            time.sleep(interval)
    except KeyboardInterrupt:
        print("stopped")
    finally:
        sock.close()


if __name__ == "__main__":
    main()
