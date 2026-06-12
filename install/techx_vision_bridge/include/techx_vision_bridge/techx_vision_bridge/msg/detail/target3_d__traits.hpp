// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from techx_vision_bridge:msg/Target3D.idl
// generated code does not contain a copyright notice

#ifndef TECHX_VISION_BRIDGE__MSG__DETAIL__TARGET3_D__TRAITS_HPP_
#define TECHX_VISION_BRIDGE__MSG__DETAIL__TARGET3_D__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "techx_vision_bridge/msg/detail/target3_d__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

// Include directives for member types
// Member 'header'
#include "std_msgs/msg/detail/header__traits.hpp"

namespace techx_vision_bridge
{

namespace msg
{

inline void to_flow_style_yaml(
  const Target3D & msg,
  std::ostream & out)
{
  out << "{";
  // member: header
  {
    out << "header: ";
    to_flow_style_yaml(msg.header, out);
    out << ", ";
  }

  // member: track_id
  {
    out << "track_id: ";
    rosidl_generator_traits::value_to_yaml(msg.track_id, out);
    out << ", ";
  }

  // member: x
  {
    out << "x: ";
    rosidl_generator_traits::value_to_yaml(msg.x, out);
    out << ", ";
  }

  // member: y
  {
    out << "y: ";
    rosidl_generator_traits::value_to_yaml(msg.y, out);
    out << ", ";
  }

  // member: z
  {
    out << "z: ";
    rosidl_generator_traits::value_to_yaml(msg.z, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const Target3D & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: header
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "header:\n";
    to_block_style_yaml(msg.header, out, indentation + 2);
  }

  // member: track_id
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "track_id: ";
    rosidl_generator_traits::value_to_yaml(msg.track_id, out);
    out << "\n";
  }

  // member: x
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "x: ";
    rosidl_generator_traits::value_to_yaml(msg.x, out);
    out << "\n";
  }

  // member: y
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "y: ";
    rosidl_generator_traits::value_to_yaml(msg.y, out);
    out << "\n";
  }

  // member: z
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "z: ";
    rosidl_generator_traits::value_to_yaml(msg.z, out);
    out << "\n";
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const Target3D & msg, bool use_flow_style = false)
{
  std::ostringstream out;
  if (use_flow_style) {
    to_flow_style_yaml(msg, out);
  } else {
    to_block_style_yaml(msg, out);
  }
  return out.str();
}

}  // namespace msg

}  // namespace techx_vision_bridge

namespace rosidl_generator_traits
{

[[deprecated("use techx_vision_bridge::msg::to_block_style_yaml() instead")]]
inline void to_yaml(
  const techx_vision_bridge::msg::Target3D & msg,
  std::ostream & out, size_t indentation = 0)
{
  techx_vision_bridge::msg::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use techx_vision_bridge::msg::to_yaml() instead")]]
inline std::string to_yaml(const techx_vision_bridge::msg::Target3D & msg)
{
  return techx_vision_bridge::msg::to_yaml(msg);
}

template<>
inline const char * data_type<techx_vision_bridge::msg::Target3D>()
{
  return "techx_vision_bridge::msg::Target3D";
}

template<>
inline const char * name<techx_vision_bridge::msg::Target3D>()
{
  return "techx_vision_bridge/msg/Target3D";
}

template<>
struct has_fixed_size<techx_vision_bridge::msg::Target3D>
  : std::integral_constant<bool, has_fixed_size<std_msgs::msg::Header>::value> {};

template<>
struct has_bounded_size<techx_vision_bridge::msg::Target3D>
  : std::integral_constant<bool, has_bounded_size<std_msgs::msg::Header>::value> {};

template<>
struct is_message<techx_vision_bridge::msg::Target3D>
  : std::true_type {};

}  // namespace rosidl_generator_traits

#endif  // TECHX_VISION_BRIDGE__MSG__DETAIL__TARGET3_D__TRAITS_HPP_
