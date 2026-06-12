// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from techx_vision_bridge:msg/Target3D.idl
// generated code does not contain a copyright notice

#ifndef TECHX_VISION_BRIDGE__MSG__DETAIL__TARGET3_D__BUILDER_HPP_
#define TECHX_VISION_BRIDGE__MSG__DETAIL__TARGET3_D__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "techx_vision_bridge/msg/detail/target3_d__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace techx_vision_bridge
{

namespace msg
{

namespace builder
{

class Init_Target3D_z
{
public:
  explicit Init_Target3D_z(::techx_vision_bridge::msg::Target3D & msg)
  : msg_(msg)
  {}
  ::techx_vision_bridge::msg::Target3D z(::techx_vision_bridge::msg::Target3D::_z_type arg)
  {
    msg_.z = std::move(arg);
    return std::move(msg_);
  }

private:
  ::techx_vision_bridge::msg::Target3D msg_;
};

class Init_Target3D_y
{
public:
  explicit Init_Target3D_y(::techx_vision_bridge::msg::Target3D & msg)
  : msg_(msg)
  {}
  Init_Target3D_z y(::techx_vision_bridge::msg::Target3D::_y_type arg)
  {
    msg_.y = std::move(arg);
    return Init_Target3D_z(msg_);
  }

private:
  ::techx_vision_bridge::msg::Target3D msg_;
};

class Init_Target3D_x
{
public:
  explicit Init_Target3D_x(::techx_vision_bridge::msg::Target3D & msg)
  : msg_(msg)
  {}
  Init_Target3D_y x(::techx_vision_bridge::msg::Target3D::_x_type arg)
  {
    msg_.x = std::move(arg);
    return Init_Target3D_y(msg_);
  }

private:
  ::techx_vision_bridge::msg::Target3D msg_;
};

class Init_Target3D_track_id
{
public:
  explicit Init_Target3D_track_id(::techx_vision_bridge::msg::Target3D & msg)
  : msg_(msg)
  {}
  Init_Target3D_x track_id(::techx_vision_bridge::msg::Target3D::_track_id_type arg)
  {
    msg_.track_id = std::move(arg);
    return Init_Target3D_x(msg_);
  }

private:
  ::techx_vision_bridge::msg::Target3D msg_;
};

class Init_Target3D_header
{
public:
  Init_Target3D_header()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_Target3D_track_id header(::techx_vision_bridge::msg::Target3D::_header_type arg)
  {
    msg_.header = std::move(arg);
    return Init_Target3D_track_id(msg_);
  }

private:
  ::techx_vision_bridge::msg::Target3D msg_;
};

}  // namespace builder

}  // namespace msg

template<typename MessageType>
auto build();

template<>
inline
auto build<::techx_vision_bridge::msg::Target3D>()
{
  return techx_vision_bridge::msg::builder::Init_Target3D_header();
}

}  // namespace techx_vision_bridge

#endif  // TECHX_VISION_BRIDGE__MSG__DETAIL__TARGET3_D__BUILDER_HPP_
