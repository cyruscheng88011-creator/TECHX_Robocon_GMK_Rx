# One detected object decoded from a Jetson vision frame.
# Coordinate convention: x/y/z are in the Jetson camera frame, meters.
# Pixel convention: u/v are image pixels, align_err_* are normalized to image center.

uint8 ZONE_UNKNOWN=0
uint8 ZONE_WEAPON_HEAD=1
uint8 ZONE_KFS=2
uint8 ZONE_QR=3

uint8 TYPE_UNKNOWN=0
uint8 TYPE_WEAPON_HEAD=1
uint8 TYPE_KFS=2
uint8 TYPE_QR=3

uint8 COLOR_UNKNOWN=0
uint8 COLOR_RED=1
uint8 COLOR_BLUE=2

std_msgs/Header header
uint32 seq
uint8 target_index
uint8 target_count
uint8 zone_id
uint8 target_type
uint8 class_id
uint8 color
float32 confidence
float32 u
float32 v
bool valid_xyz
float32 x
float32 y
float32 z
float32 align_err_x
float32 align_err_y
float32 priority
