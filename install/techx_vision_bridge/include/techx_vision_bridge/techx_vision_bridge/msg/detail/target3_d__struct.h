// NOLINT: This file starts with a BOM since it contain non-ASCII characters
// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from techx_vision_bridge:msg/Target3D.idl
// generated code does not contain a copyright notice

#ifndef TECHX_VISION_BRIDGE__MSG__DETAIL__TARGET3_D__STRUCT_H_
#define TECHX_VISION_BRIDGE__MSG__DETAIL__TARGET3_D__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// Constants defined in the message

// Include directives for member types
// Member 'header'
#include "std_msgs/msg/detail/header__struct.h"

/// Struct defined in msg/Target3D in the package techx_vision_bridge.
/**
  * 三维目标检测信息
  * 由 techx_vision_bridge 节点发布，携带上游视觉系统解算的单目标位姿
 */
typedef struct techx_vision_bridge__msg__Target3D
{
  /// ROS 2 标准头（含时间戳和 frame_id）
  std_msgs__msg__Header header;
  /// 目标跟踪 ID（0~255），在 frame_id 中也有体现
  uint8_t track_id;
  /// 目标在相机坐标系下的 X 坐标（米）
  float x;
  /// 目标在相机坐标系下的 Y 坐标（米）
  float y;
  /// 目标在相机坐标系下的 Z 坐标（米）
  float z;
} techx_vision_bridge__msg__Target3D;

// Struct for a sequence of techx_vision_bridge__msg__Target3D.
typedef struct techx_vision_bridge__msg__Target3D__Sequence
{
  techx_vision_bridge__msg__Target3D * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} techx_vision_bridge__msg__Target3D__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // TECHX_VISION_BRIDGE__MSG__DETAIL__TARGET3_D__STRUCT_H_
