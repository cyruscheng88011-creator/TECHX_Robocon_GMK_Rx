// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from techx_vision_bridge:msg/Target3D.idl
// generated code does not contain a copyright notice

#ifndef TECHX_VISION_BRIDGE__MSG__DETAIL__TARGET3_D__STRUCT_HPP_
#define TECHX_VISION_BRIDGE__MSG__DETAIL__TARGET3_D__STRUCT_HPP_

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rosidl_runtime_cpp/bounded_vector.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


// Include directives for member types
// Member 'header'
#include "std_msgs/msg/detail/header__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__techx_vision_bridge__msg__Target3D __attribute__((deprecated))
#else
# define DEPRECATED__techx_vision_bridge__msg__Target3D __declspec(deprecated)
#endif

namespace techx_vision_bridge
{

namespace msg
{

// message struct
template<class ContainerAllocator>
struct Target3D_
{
  using Type = Target3D_<ContainerAllocator>;

  explicit Target3D_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : header(_init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->track_id = 0;
      this->x = 0.0f;
      this->y = 0.0f;
      this->z = 0.0f;
    }
  }

  explicit Target3D_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : header(_alloc, _init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->track_id = 0;
      this->x = 0.0f;
      this->y = 0.0f;
      this->z = 0.0f;
    }
  }

  // field types and members
  using _header_type =
    std_msgs::msg::Header_<ContainerAllocator>;
  _header_type header;
  using _track_id_type =
    uint8_t;
  _track_id_type track_id;
  using _x_type =
    float;
  _x_type x;
  using _y_type =
    float;
  _y_type y;
  using _z_type =
    float;
  _z_type z;

  // setters for named parameter idiom
  Type & set__header(
    const std_msgs::msg::Header_<ContainerAllocator> & _arg)
  {
    this->header = _arg;
    return *this;
  }
  Type & set__track_id(
    const uint8_t & _arg)
  {
    this->track_id = _arg;
    return *this;
  }
  Type & set__x(
    const float & _arg)
  {
    this->x = _arg;
    return *this;
  }
  Type & set__y(
    const float & _arg)
  {
    this->y = _arg;
    return *this;
  }
  Type & set__z(
    const float & _arg)
  {
    this->z = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    techx_vision_bridge::msg::Target3D_<ContainerAllocator> *;
  using ConstRawPtr =
    const techx_vision_bridge::msg::Target3D_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<techx_vision_bridge::msg::Target3D_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<techx_vision_bridge::msg::Target3D_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      techx_vision_bridge::msg::Target3D_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<techx_vision_bridge::msg::Target3D_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      techx_vision_bridge::msg::Target3D_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<techx_vision_bridge::msg::Target3D_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<techx_vision_bridge::msg::Target3D_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<techx_vision_bridge::msg::Target3D_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__techx_vision_bridge__msg__Target3D
    std::shared_ptr<techx_vision_bridge::msg::Target3D_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__techx_vision_bridge__msg__Target3D
    std::shared_ptr<techx_vision_bridge::msg::Target3D_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const Target3D_ & other) const
  {
    if (this->header != other.header) {
      return false;
    }
    if (this->track_id != other.track_id) {
      return false;
    }
    if (this->x != other.x) {
      return false;
    }
    if (this->y != other.y) {
      return false;
    }
    if (this->z != other.z) {
      return false;
    }
    return true;
  }
  bool operator!=(const Target3D_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct Target3D_

// alias to use template instance with default allocator
using Target3D =
  techx_vision_bridge::msg::Target3D_<std::allocator<void>>;

// constant definitions

}  // namespace msg

}  // namespace techx_vision_bridge

#endif  // TECHX_VISION_BRIDGE__MSG__DETAIL__TARGET3_D__STRUCT_HPP_
