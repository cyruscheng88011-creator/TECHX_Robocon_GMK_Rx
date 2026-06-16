# UDP V2 bridge

The bridge accepts legacy 0x55AA packets and V2 0x55AB packets.

V2 header: magic, version, flags, seq, timestamp, count.
V2 target: track_id, class_id, color, confidence, u, v, x, y, z.

count=0 means the sender is alive but there is no fresh target in the current inference frame.
z=0 means recognition data is present but the 3D coordinate is not valid.
