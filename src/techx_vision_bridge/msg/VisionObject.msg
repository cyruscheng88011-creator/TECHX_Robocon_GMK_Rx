# One detected object decoded from a Jetson vision frame.
# Raw x/y/z are always in the Jetson camera frame, meters.
# control_x/y/z are transformed for the consumer that should use the object.

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

uint8 FRAME_UNKNOWN=0
uint8 FRAME_CAMERA=1
uint8 FRAME_ROBOT_BASE=2
uint8 FRAME_ARM1_BASE=3
uint8 FRAME_ARM2_BASE=4

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

bool valid_robot_xyz
float32 robot_x
float32 robot_y
float32 robot_z

uint8 control_frame
bool valid_control_xyz
float32 control_x
float32 control_y
float32 control_z

float32 align_err_x
float32 align_err_y
float32 priority
