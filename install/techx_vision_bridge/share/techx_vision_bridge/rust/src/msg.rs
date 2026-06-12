#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};



// Corresponds to techx_vision_bridge__msg__Target3D
/// 三维目标检测信息
/// 由 techx_vision_bridge 节点发布，携带上游视觉系统解算的单目标位姿

#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct Target3D {
    /// ROS 2 标准头（含时间戳和 frame_id）
    pub header: std_msgs::msg::Header,

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
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::msg::rmw::Target3D::default())
  }
}

impl rosidl_runtime_rs::Message for Target3D {
  type RmwMsg = super::msg::rmw::Target3D;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        header: std_msgs::msg::Header::into_rmw_message(std::borrow::Cow::Owned(msg.header)).into_owned(),
        track_id: msg.track_id,
        x: msg.x,
        y: msg.y,
        z: msg.z,
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        header: std_msgs::msg::Header::into_rmw_message(std::borrow::Cow::Borrowed(&msg.header)).into_owned(),
      track_id: msg.track_id,
      x: msg.x,
      y: msg.y,
      z: msg.z,
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      header: std_msgs::msg::Header::from_rmw_message(msg.header),
      track_id: msg.track_id,
      x: msg.x,
      y: msg.y,
      z: msg.z,
    }
  }
}


