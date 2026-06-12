// generated from rosidl_typesupport_fastrtps_cpp/resource/idl__rosidl_typesupport_fastrtps_cpp.hpp.em
// with input from techx_vision_bridge:msg/Target3D.idl
// generated code does not contain a copyright notice

#ifndef TECHX_VISION_BRIDGE__MSG__DETAIL__TARGET3_D__ROSIDL_TYPESUPPORT_FASTRTPS_CPP_HPP_
#define TECHX_VISION_BRIDGE__MSG__DETAIL__TARGET3_D__ROSIDL_TYPESUPPORT_FASTRTPS_CPP_HPP_

#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rosidl_typesupport_interface/macros.h"
#include "techx_vision_bridge/msg/rosidl_typesupport_fastrtps_cpp__visibility_control.h"
#include "techx_vision_bridge/msg/detail/target3_d__struct.hpp"

#ifndef _WIN32
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wunused-parameter"
# ifdef __clang__
#  pragma clang diagnostic ignored "-Wdeprecated-register"
#  pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
# endif
#endif
#ifndef _WIN32
# pragma GCC diagnostic pop
#endif

#include "fastcdr/Cdr.h"

namespace techx_vision_bridge
{

namespace msg
{

namespace typesupport_fastrtps_cpp
{

bool
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_techx_vision_bridge
cdr_serialize(
  const techx_vision_bridge::msg::Target3D & ros_message,
  eprosima::fastcdr::Cdr & cdr);

bool
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_techx_vision_bridge
cdr_deserialize(
  eprosima::fastcdr::Cdr & cdr,
  techx_vision_bridge::msg::Target3D & ros_message);

size_t
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_techx_vision_bridge
get_serialized_size(
  const techx_vision_bridge::msg::Target3D & ros_message,
  size_t current_alignment);

size_t
ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_techx_vision_bridge
max_serialized_size_Target3D(
  bool & full_bounded,
  bool & is_plain,
  size_t current_alignment);

}  // namespace typesupport_fastrtps_cpp

}  // namespace msg

}  // namespace techx_vision_bridge

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_FASTRTPS_CPP_PUBLIC_techx_vision_bridge
const rosidl_message_type_support_t *
  ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, techx_vision_bridge, msg, Target3D)();

#ifdef __cplusplus
}
#endif

#endif  // TECHX_VISION_BRIDGE__MSG__DETAIL__TARGET3_D__ROSIDL_TYPESUPPORT_FASTRTPS_CPP_HPP_
