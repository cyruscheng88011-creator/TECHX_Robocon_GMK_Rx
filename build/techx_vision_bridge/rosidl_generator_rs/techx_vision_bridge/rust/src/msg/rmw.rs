#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};


#[link(name = "techx_vision_bridge__rosidl_typesupport_c")]
extern "C" {
    fn rosidl_typesupport_c__get_message_type_support_handle__techx_vision_bridge__msg__Target3D() -> *const std::ffi::c_void;
}

#[link(name = "techx_vision_bridge__rosidl_generator_c")]
extern "C" {
    fn techx_vision_bridge__msg__Target3D__init(msg: *mut Target3D) -> bool;
    fn techx_vision_bridge__msg__Target3D__Sequence__init(seq: *mut rosidl_runtime_rs::Sequence<Target3D>, size: usize) -> bool;
    fn techx_vision_bridge__msg__Target3D__Sequence__fini(seq: *mut rosidl_runtime_rs::Sequence<Target3D>);
    fn techx_vision_bridge__msg__Target3D__Sequence__copy(in_seq: &rosidl_runtime_rs::Sequence<Target3D>, out_seq: *mut rosidl_runtime_rs::Sequence<Target3D>) -> bool;
}

// Corresponds to techx_vision_bridge__msg__Target3D
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]

/// 三维目标检测信息
/// 由 techx_vision_bridge 节点发布，携带上游视觉系统解算的单目标位姿

#[repr(C)]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Target3D {
    /// ROS 2 标准头（含时间戳和 frame_id）
    pub header: std_msgs::msg::rmw::Header,

    /// 目标跟踪 ID（0~255），在 frame_id 中也有体现
    pub track_id: u8,

    /// 目标在相机坐标系下的 X 坐标（米）
    pub x: f32,

    /// 目标在相机坐标系下的 Y 坐标（米）
    pub y: f32,

    /// 目标在相机坐标系下的 Z 坐标（米）
    pub z: f32,

}



impl Default for Target3D {
  fn default() -> Self {
    unsafe {
      let mut msg = std::mem::zeroed();
      if !techx_vision_bridge__msg__Target3D__init(&mut msg as *mut _) {
        panic!("Call to techx_vision_bridge__msg__Target3D__init() failed");
      }
      msg
    }
  }
}

impl rosidl_runtime_rs::SequenceAlloc for Target3D {
  fn sequence_init(seq: &mut rosidl_runtime_rs::Sequence<Self>, size: usize) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { techx_vision_bridge__msg__Target3D__Sequence__init(seq as *mut _, size) }
  }
  fn sequence_fini(seq: &mut rosidl_runtime_rs::Sequence<Self>) {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { techx_vision_bridge__msg__Target3D__Sequence__fini(seq as *mut _) }
  }
  fn sequence_copy(in_seq: &rosidl_runtime_rs::Sequence<Self>, out_seq: &mut rosidl_runtime_rs::Sequence<Self>) -> bool {
    // SAFETY: This is safe since the pointer is guaranteed to be valid/initialized.
    unsafe { techx_vision_bridge__msg__Target3D__Sequence__copy(in_seq, out_seq as *mut _) }
  }
}

impl rosidl_runtime_rs::Message for Target3D {
  type RmwMsg = Self;
  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> { msg_cow }
  fn from_rmw_message(msg: Self::RmwMsg) -> Self { msg }
}

impl rosidl_runtime_rs::RmwMessage for Target3D where Self: Sized {
  const TYPE_NAME: &'static str = "techx_vision_bridge/msg/Target3D";
  fn get_type_support() -> *const std::ffi::c_void {
    // SAFETY: No preconditions for this function.
    unsafe { rosidl_typesupport_c__get_message_type_support_handle__techx_vision_bridge__msg__Target3D() }
  }
}


