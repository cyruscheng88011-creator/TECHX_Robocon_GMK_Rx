#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "rclcpp/rclcpp.hpp"
#include "techx_vision_bridge/msg/target3_d.hpp"
#include "techx_vision_bridge/msg/vision_frame.hpp"
#include "techx_vision_bridge/msg/vision_object.hpp"
#include "techx_vision_bridge/msg/vision_target.hpp"

namespace {
constexpr uint16_t MAGIC_LEGACY = 0x55AA;
constexpr uint16_t MAGIC_V2 = 0x55AB;
constexpr uint8_t VERSION_V2 = 2;
constexpr size_t MAX_TARGETS = 16;

constexpr uint8_t ZONE_UNKNOWN = 0;
constexpr uint8_t ZONE_HEAD = 1;
constexpr uint8_t ZONE_KFS = 2;
constexpr uint8_t ZONE_QR = 3;

constexpr uint8_t TYPE_UNKNOWN = 0;
constexpr uint8_t TYPE_HEAD = 1;
constexpr uint8_t TYPE_KFS = 2;
constexpr uint8_t TYPE_QR = 3;

constexpr uint8_t FRAME_UNKNOWN = 0;
constexpr uint8_t FRAME_CAMERA_LINK = 1;
constexpr uint8_t FRAME_ROBOT_BASE = 2;
constexpr uint8_t FRAME_ARM1_BASE = 3;
constexpr uint8_t FRAME_ARM2_BASE = 4;

#pragma pack(push, 1)
struct LegacyPacket {
  uint16_t magic;
  uint32_t seq;
  double timestamp;
  uint8_t track_id;
  float x;
  float y;
  float z;
  uint16_t crc16;
};

struct V2Header {
  uint16_t magic;
  uint8_t version;
  uint8_t flags;
  uint32_t seq;
  double timestamp;
  uint8_t count;
};

struct V2Target {
  uint8_t track_id;
  uint8_t class_id;
  uint8_t color;
  float confidence;
  float u;
  float v;
  float x;
  float y;
  float z;
};
#pragma pack(pop)

static_assert(sizeof(LegacyPacket) == 29, "legacy size");
static_assert(sizeof(V2Header) == 17, "v2 header size");
static_assert(sizeof(V2Target) == 27, "v2 target size");

struct Vec3 {
  double x{0.0};
  double y{0.0};
  double z{0.0};
};

struct Transform {
  std::array<std::array<double, 3>, 3> r{{
      std::array<double, 3>{1.0, 0.0, 0.0},
      std::array<double, 3>{0.0, 1.0, 0.0},
      std::array<double, 3>{0.0, 0.0, 1.0},
  }};
  Vec3 t{};
};

struct DecodedTarget {
  uint8_t track_id{0};
  uint8_t class_id{255};
  uint8_t color{0};
  float confidence{0.0f};
  float u{0.0f};
  float v{0.0f};
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
  bool valid_xyz{false};

  bool valid_robot_xyz{false};
  float robot_x{0.0f};
  float robot_y{0.0f};
  float robot_z{0.0f};

  bool valid_arm1_xyz{false};
  float arm1_x{0.0f};
  float arm1_y{0.0f};
  float arm1_z{0.0f};

  bool valid_arm2_xyz{false};
  float arm2_x{0.0f};
  float arm2_y{0.0f};
  float arm2_z{0.0f};

  uint8_t zone_id{ZONE_UNKNOWN};
  uint8_t target_type{TYPE_UNKNOWN};
  uint8_t control_frame{FRAME_UNKNOWN};
  bool valid_control_xyz{false};
  float control_x{0.0f};
  float control_y{0.0f};
  float control_z{0.0f};

  float align_err_x{0.0f};
  float align_err_y{0.0f};
  float priority{0.0f};
};

struct DecodedFrame {
  uint8_t protocol_version{0};
  uint32_t seq{0};
  double timestamp{0.0};
  std::vector<DecodedTarget> targets;
  std::chrono::system_clock::time_point recv_time;
};

constexpr std::array<uint16_t, 256> make_crc_table() {
  std::array<uint16_t, 256> table{};
  for (int i = 0; i < 256; ++i) {
    uint16_t crc = static_cast<uint16_t>(i << 8);
    for (int j = 0; j < 8; ++j) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
    table[i] = crc;
  }
  return table;
}

constexpr auto CRC_TABLE = make_crc_table();

uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc = static_cast<uint16_t>(((crc << 8) & 0xFFFF) ^ CRC_TABLE[((crc >> 8) ^ data[i]) & 0xFF]);
  }
  return crc;
}

bool finite3(float x, float y, float z) {
  return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

Transform make_transform_from_xyz_rpy(const std::vector<double> &p) {
  Transform tf{};
  if (p.size() != 6) {
    return tf;
  }
  tf.t = Vec3{p[0], p[1], p[2]};
  const double cr = std::cos(p[3]);
  const double sr = std::sin(p[3]);
  const double cp = std::cos(p[4]);
  const double sp = std::sin(p[4]);
  const double cy = std::cos(p[5]);
  const double sy = std::sin(p[5]);

  // R = Rz(yaw) * Ry(pitch) * Rx(roll). point_out = R * point_in + t.
  tf.r = {{{cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr},
           {sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr},
           {-sp, cp * sr, cp * cr}}};
  return tf;
}

Vec3 apply_transform(const Transform &tf, const Vec3 &p) {
  return Vec3{
      tf.r[0][0] * p.x + tf.r[0][1] * p.y + tf.r[0][2] * p.z + tf.t.x,
      tf.r[1][0] * p.x + tf.r[1][1] * p.y + tf.r[1][2] * p.z + tf.t.y,
      tf.r[2][0] * p.x + tf.r[2][1] * p.y + tf.r[2][2] * p.z + tf.t.z,
  };
}
}  // namespace

class VisionFrameBridgeNode : public rclcpp::Node {
 public:
  VisionFrameBridgeNode() : Node("vision_bridge_node") {
    declare_parameter("udp_bind_addr", "0.0.0.0");
    declare_parameter("udp_port", 12345);
    declare_parameter("frame_topic_name", "/techx/vision/frame");
    declare_parameter("object_topic_name", "/techx/vision/objects");
    declare_parameter("detail_topic_name", "/techx/vision/kfs_targets");
    declare_parameter("topic_name", "/techx/vision/targets");
    declare_parameter("publish_frame_topic", true);
    declare_parameter("publish_object_topic", true);
    declare_parameter("publish_detail_topic", true);
    declare_parameter("publish_legacy_topic", true);
    declare_parameter("accept_legacy", false);
    declare_parameter("image_width", 640.0);
    declare_parameter("image_height", 480.0);
    declare_parameter("head_class_min", 100);
    declare_parameter("head_class_max", 149);
    declare_parameter("qr_class_id", 200);
    declare_parameter("enable_transforms", true);
    declare_parameter<std::vector<double>>("T_robot_camera_xyz_rpy", {0.0, 0.0, 0.0, 0.0, 0.0, 0.0});
    declare_parameter<std::vector<double>>("T_arm1_robot_xyz_rpy", {0.0, 0.0, 0.0, 0.0, 0.0, 0.0});
    declare_parameter<std::vector<double>>("T_arm2_robot_xyz_rpy", {0.0, 0.0, 0.0, 0.0, 0.0, 0.0});
    declare_parameter("watchdog_timeout_sec", 0.3);

    bind_addr_ = get_parameter("udp_bind_addr").as_string();
    udp_port_ = get_parameter("udp_port").as_int();
    publish_frame_ = get_parameter("publish_frame_topic").as_bool();
    publish_object_ = get_parameter("publish_object_topic").as_bool();
    publish_detail_ = get_parameter("publish_detail_topic").as_bool();
    publish_legacy_ = get_parameter("publish_legacy_topic").as_bool();
    accept_legacy_ = get_parameter("accept_legacy").as_bool();
    image_width_ = get_parameter("image_width").as_double();
    image_height_ = get_parameter("image_height").as_double();
    head_class_min_ = get_parameter("head_class_min").as_int();
    head_class_max_ = get_parameter("head_class_max").as_int();
    qr_class_id_ = get_parameter("qr_class_id").as_int();
    enable_transforms_ = get_parameter("enable_transforms").as_bool();
    tf_robot_camera_ = make_transform_from_xyz_rpy(get_parameter("T_robot_camera_xyz_rpy").as_double_array());
    tf_arm1_robot_ = make_transform_from_xyz_rpy(get_parameter("T_arm1_robot_xyz_rpy").as_double_array());
    tf_arm2_robot_ = make_transform_from_xyz_rpy(get_parameter("T_arm2_robot_xyz_rpy").as_double_array());
    watchdog_timeout_sec_ = get_parameter("watchdog_timeout_sec").as_double();

    auto qos = rclcpp::SensorDataQoS();
    frame_pub_ = create_publisher<techx_vision_bridge::msg::VisionFrame>(get_parameter("frame_topic_name").as_string(), qos);
    object_pub_ = create_publisher<techx_vision_bridge::msg::VisionObject>(get_parameter("object_topic_name").as_string(), qos);
    detail_pub_ = create_publisher<techx_vision_bridge::msg::VisionTarget>(get_parameter("detail_topic_name").as_string(), qos);
    legacy_pub_ = create_publisher<techx_vision_bridge::msg::Target3D>(get_parameter("topic_name").as_string(), qos);

    open_socket();
    recv_timer_ = create_wall_timer(std::chrono::milliseconds(2), std::bind(&VisionFrameBridgeNode::recv_once, this));
    watchdog_timer_ = create_wall_timer(std::chrono::milliseconds(200), std::bind(&VisionFrameBridgeNode::watchdog, this));
    last_valid_ = std::chrono::steady_clock::now();
    RCLCPP_INFO(get_logger(), "vision bridge ready udp=%s:%d accept_legacy=%s transforms=%s", bind_addr_.c_str(), udp_port_, accept_legacy_ ? "true" : "false", enable_transforms_ ? "true" : "false");
  }

  ~VisionFrameBridgeNode() override {
    if (sock_ >= 0) {
      close(sock_);
    }
  }

 private:
  void open_socket() {
    if (sock_ >= 0) {
      close(sock_);
      sock_ = -1;
    }
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) {
      RCLCPP_ERROR(get_logger(), "socket failed: %s", std::strerror(errno));
      return;
    }
    int opt = 1;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    int flags = fcntl(sock_, F_GETFL, 0);
    if (flags >= 0) {
      fcntl(sock_, F_SETFL, flags | O_NONBLOCK);
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(udp_port_));
    if (inet_pton(AF_INET, bind_addr_.c_str(), &addr.sin_addr) != 1) {
      RCLCPP_ERROR(get_logger(), "bad bind address: %s", bind_addr_.c_str());
      close(sock_);
      sock_ = -1;
      return;
    }
    if (bind(sock_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
      RCLCPP_ERROR(get_logger(), "bind failed: %s", std::strerror(errno));
      close(sock_);
      sock_ = -1;
    }
  }

  void recv_once() {
    if (sock_ < 0) {
      open_socket();
      return;
    }

    uint8_t buf[4096];
    DecodedFrame best;
    bool has_best = false;
    while (true) {
      ssize_t n = recvfrom(sock_, buf, sizeof(buf), 0, nullptr, nullptr);
      if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000, "recvfrom failed: %s", std::strerror(errno));
        }
        break;
      }
      DecodedFrame frame;
      if (decode(buf, static_cast<size_t>(n), frame) && prefer(frame, best, has_best)) {
        best = std::move(frame);
        has_best = true;
      }
    }
    if (has_best) {
      last_valid_ = std::chrono::steady_clock::now();
      publish(best);
    }
  }

  bool prefer(const DecodedFrame &candidate, const DecodedFrame &current, bool has_current) const {
    if (!has_current) return true;
    if (candidate.protocol_version == 2 && current.protocol_version != 2) return true;
    if (candidate.protocol_version != 2 && current.protocol_version == 2) return false;
    return static_cast<int32_t>(candidate.seq - current.seq) > 0;
  }

  bool accept_seq(uint32_t seq, uint8_t version) {
    if (version == 2) {
      if (!v2_seq_init_ || static_cast<int32_t>(seq - last_v2_seq_) > 0) {
        last_v2_seq_ = seq;
        v2_seq_init_ = true;
        v2_seen_ = true;
        return true;
      }
      return false;
    }
    if (!accept_legacy_ || v2_seen_) return false;
    if (!legacy_seq_init_ || static_cast<int32_t>(seq - last_legacy_seq_) > 0) {
      last_legacy_seq_ = seq;
      legacy_seq_init_ = true;
      return true;
    }
    return false;
  }

  bool decode(const uint8_t *data, size_t len, DecodedFrame &out) {
    if (len < sizeof(uint16_t)) return false;
    uint16_t magic = 0;
    std::memcpy(&magic, data, sizeof(magic));
    if (magic == MAGIC_V2) return decode_v2(data, len, out);
    if (magic == MAGIC_LEGACY) return decode_legacy(data, len, out);
    return false;
  }

  bool decode_legacy(const uint8_t *data, size_t len, DecodedFrame &out) {
    if (!accept_legacy_ || len != sizeof(LegacyPacket)) return false;
    LegacyPacket pkt{};
    std::memcpy(&pkt, data, sizeof(pkt));
    if (crc16_ccitt(data, 27) != pkt.crc16) return false;
    if (!accept_seq(pkt.seq, 1)) return false;
    out = DecodedFrame{};
    out.protocol_version = 1;
    out.seq = pkt.seq;
    out.timestamp = pkt.timestamp;
    out.recv_time = std::chrono::system_clock::now();
    DecodedTarget t{};
    t.track_id = pkt.track_id;
    t.confidence = 1.0f;
    t.x = pkt.x;
    t.y = pkt.y;
    t.z = pkt.z;
    t.valid_xyz = pkt.z > 0.0f && finite3(pkt.x, pkt.y, pkt.z);
    enrich(t);
    out.targets.push_back(t);
    return true;
  }

  bool decode_v2(const uint8_t *data, size_t len, DecodedFrame &out) {
    if (len < sizeof(V2Header) + sizeof(uint16_t)) return false;
    V2Header h{};
    std::memcpy(&h, data, sizeof(h));
    if (h.version != VERSION_V2 || h.count > MAX_TARGETS) return false;
    size_t expected = sizeof(V2Header) + static_cast<size_t>(h.count) * sizeof(V2Target) + sizeof(uint16_t);
    if (len != expected) return false;
    uint16_t rx_crc = 0;
    std::memcpy(&rx_crc, data + len - sizeof(uint16_t), sizeof(rx_crc));
    if (crc16_ccitt(data, len - sizeof(uint16_t)) != rx_crc) return false;
    if (!accept_seq(h.seq, 2)) return false;

    out = DecodedFrame{};
    out.protocol_version = 2;
    out.seq = h.seq;
    out.timestamp = h.timestamp;
    out.recv_time = std::chrono::system_clock::now();
    out.targets.reserve(h.count);
    size_t off = sizeof(V2Header);
    for (uint8_t i = 0; i < h.count; ++i) {
      V2Target raw{};
      std::memcpy(&raw, data + off, sizeof(raw));
      off += sizeof(raw);
      DecodedTarget t{};
      t.track_id = raw.track_id;
      t.class_id = raw.class_id;
      t.color = raw.color;
      t.confidence = raw.confidence;
      t.u = raw.u;
      t.v = raw.v;
      t.x = raw.x;
      t.y = raw.y;
      t.z = raw.z;
      t.valid_xyz = raw.z > 0.0f && finite3(raw.x, raw.y, raw.z);
      enrich(t);
      out.targets.push_back(t);
    }
    return true;
  }

  void enrich(DecodedTarget &t) {
    if (t.class_id <= 4) {
      t.zone_id = ZONE_KFS;
      t.target_type = TYPE_KFS;
    } else if (t.class_id == static_cast<uint8_t>(qr_class_id_)) {
      t.zone_id = ZONE_QR;
      t.target_type = TYPE_QR;
    } else if (t.class_id >= static_cast<uint8_t>(head_class_min_) && t.class_id <= static_cast<uint8_t>(head_class_max_)) {
      t.zone_id = ZONE_HEAD;
      t.target_type = TYPE_HEAD;
    }

    if (image_width_ > 1.0 && image_height_ > 1.0 && std::isfinite(t.u) && std::isfinite(t.v)) {
      const double cx = image_width_ * 0.5;
      const double cy = image_height_ * 0.5;
      t.align_err_x = static_cast<float>((static_cast<double>(t.u) - cx) / cx);
      t.align_err_y = static_cast<float>((static_cast<double>(t.v) - cy) / cy);
    }

    fill_transformed_coordinates(t);

    float score = std::isfinite(t.confidence) ? t.confidence : 0.0f;
    score -= 0.15f * std::fabs(t.align_err_x);
    score -= 0.10f * std::fabs(t.align_err_y);
    if (t.valid_control_xyz) score += 0.05f * std::max(0.0f, 2.0f - t.control_z);
    t.priority = score;
  }

  void fill_transformed_coordinates(DecodedTarget &t) const {
    t.control_frame = preferred_frame(t.target_type);
    if (!t.valid_xyz) {
      return;
    }

    const Vec3 camera{t.x, t.y, t.z};
    if (!enable_transforms_) {
      t.control_frame = FRAME_CAMERA_LINK;
      t.valid_control_xyz = true;
      t.control_x = static_cast<float>(camera.x);
      t.control_y = static_cast<float>(camera.y);
      t.control_z = static_cast<float>(camera.z);
      return;
    }

    const Vec3 robot = apply_transform(tf_robot_camera_, camera);
    const Vec3 arm1 = apply_transform(tf_arm1_robot_, robot);
    const Vec3 arm2 = apply_transform(tf_arm2_robot_, robot);

    t.valid_robot_xyz = true;
    t.robot_x = static_cast<float>(robot.x);
    t.robot_y = static_cast<float>(robot.y);
    t.robot_z = static_cast<float>(robot.z);

    t.valid_arm1_xyz = true;
    t.arm1_x = static_cast<float>(arm1.x);
    t.arm1_y = static_cast<float>(arm1.y);
    t.arm1_z = static_cast<float>(arm1.z);

    t.valid_arm2_xyz = true;
    t.arm2_x = static_cast<float>(arm2.x);
    t.arm2_y = static_cast<float>(arm2.y);
    t.arm2_z = static_cast<float>(arm2.z);

    Vec3 control = camera;
    if (t.control_frame == FRAME_ROBOT_BASE) {
      control = robot;
    } else if (t.control_frame == FRAME_ARM1_BASE) {
      control = arm1;
    } else if (t.control_frame == FRAME_ARM2_BASE) {
      control = arm2;
    }

    t.valid_control_xyz = true;
    t.control_x = static_cast<float>(control.x);
    t.control_y = static_cast<float>(control.y);
    t.control_z = static_cast<float>(control.z);
  }

  uint8_t preferred_frame(uint8_t target_type) const {
    if (target_type == TYPE_HEAD) return FRAME_ARM1_BASE;
    if (target_type == TYPE_KFS) return FRAME_ARM2_BASE;
    if (target_type == TYPE_QR) return FRAME_ROBOT_BASE;
    return FRAME_CAMERA_LINK;
  }

  rclcpp::Time stamp(const DecodedFrame &frame) const {
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(frame.recv_time.time_since_epoch()).count();
    return rclcpp::Time(ns);
  }

  techx_vision_bridge::msg::VisionObject to_object(const DecodedFrame &frame, const DecodedTarget &t, uint8_t index) {
    techx_vision_bridge::msg::VisionObject msg;
    msg.header.stamp = stamp(frame);
    msg.header.frame_id = "camera_link";
    msg.seq = frame.seq;
    msg.target_index = index;
    msg.target_count = static_cast<uint8_t>(frame.targets.size());
    msg.zone_id = t.zone_id;
    msg.target_type = t.target_type;
    msg.class_id = t.class_id;
    msg.color = t.color;
    msg.confidence = t.confidence;
    msg.u = t.u;
    msg.v = t.v;

    msg.valid_xyz = t.valid_xyz;
    msg.x = t.valid_xyz ? t.x : 0.0f;
    msg.y = t.valid_xyz ? t.y : 0.0f;
    msg.z = t.valid_xyz ? t.z : 0.0f;

    msg.valid_robot_xyz = t.valid_robot_xyz;
    msg.robot_x = t.valid_robot_xyz ? t.robot_x : 0.0f;
    msg.robot_y = t.valid_robot_xyz ? t.robot_y : 0.0f;
    msg.robot_z = t.valid_robot_xyz ? t.robot_z : 0.0f;

    msg.valid_arm1_xyz = t.valid_arm1_xyz;
    msg.arm1_x = t.valid_arm1_xyz ? t.arm1_x : 0.0f;
    msg.arm1_y = t.valid_arm1_xyz ? t.arm1_y : 0.0f;
    msg.arm1_z = t.valid_arm1_xyz ? t.arm1_z : 0.0f;

    msg.valid_arm2_xyz = t.valid_arm2_xyz;
    msg.arm2_x = t.valid_arm2_xyz ? t.arm2_x : 0.0f;
    msg.arm2_y = t.valid_arm2_xyz ? t.arm2_y : 0.0f;
    msg.arm2_z = t.valid_arm2_xyz ? t.arm2_z : 0.0f;

    msg.control_frame = t.control_frame;
    msg.valid_control_xyz = t.valid_control_xyz;
    msg.control_x = t.valid_control_xyz ? t.control_x : 0.0f;
    msg.control_y = t.valid_control_xyz ? t.control_y : 0.0f;
    msg.control_z = t.valid_control_xyz ? t.control_z : 0.0f;

    msg.align_err_x = t.align_err_x;
    msg.align_err_y = t.align_err_y;
    msg.priority = t.priority;
    return msg;
  }

  void publish(const DecodedFrame &frame) {
    std::vector<techx_vision_bridge::msg::VisionObject> objects;
    objects.reserve(frame.targets.size());
    for (size_t i = 0; i < frame.targets.size(); ++i) {
      auto obj = to_object(frame, frame.targets[i], static_cast<uint8_t>(i));
      objects.push_back(obj);
      if (publish_object_) object_pub_->publish(obj);
      if (publish_detail_) publish_detail(frame, &frame.targets[i]);
      if (publish_legacy_ && frame.targets[i].valid_xyz) publish_xyz(frame, frame.targets[i]);
    }
    if (frame.targets.empty() && publish_detail_) publish_detail(frame, nullptr);
    if (publish_frame_) {
      techx_vision_bridge::msg::VisionFrame msg;
      msg.header.stamp = stamp(frame);
      msg.header.frame_id = "camera_link";
      msg.seq = frame.seq;
      msg.protocol_version = frame.protocol_version;
      msg.upstream_timestamp = frame.timestamp;
      msg.target_count = static_cast<uint8_t>(frame.targets.size());
      msg.has_target = !frame.targets.empty();
      msg.targets = std::move(objects);
      frame_pub_->publish(msg);
    }
  }

  void publish_detail(const DecodedFrame &frame, const DecodedTarget *t) {
    techx_vision_bridge::msg::VisionTarget msg;
    msg.header.stamp = stamp(frame);
    msg.header.frame_id = "camera_link";
    msg.seq = frame.seq;
    msg.protocol_version = frame.protocol_version;
    msg.target_count = static_cast<uint8_t>(frame.targets.size());
    msg.has_target = t != nullptr;
    msg.valid_xyz = t && t->valid_xyz;
    if (t) {
      msg.track_id = t->track_id;
      msg.class_id = t->class_id;
      msg.color = t->color;
      msg.confidence = t->confidence;
      msg.u = t->u;
      msg.v = t->v;
      msg.x = t->valid_xyz ? t->x : 0.0f;
      msg.y = t->valid_xyz ? t->y : 0.0f;
      msg.z = t->valid_xyz ? t->z : 0.0f;
    }
    detail_pub_->publish(msg);
  }

  void publish_xyz(const DecodedFrame &frame, const DecodedTarget &t) {
    techx_vision_bridge::msg::Target3D msg;
    msg.header.stamp = stamp(frame);
    msg.header.frame_id = "camera_link_" + std::to_string(t.track_id);
    msg.track_id = t.track_id;
    msg.x = t.x;
    msg.y = t.y;
    msg.z = t.z;
    legacy_pub_->publish(msg);
  }

  void watchdog() {
    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - last_valid_).count();
    if (elapsed > watchdog_timeout_sec_) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 1000, "vision UDP signal lost %.2fs", elapsed);
    }
  }

  int sock_{-1};
  std::string bind_addr_;
  int udp_port_{12345};
  bool publish_frame_{true};
  bool publish_object_{true};
  bool publish_detail_{true};
  bool publish_legacy_{true};
  bool accept_legacy_{false};
  bool enable_transforms_{true};
  double image_width_{640.0};
  double image_height_{480.0};
  int head_class_min_{100};
  int head_class_max_{149};
  int qr_class_id_{200};
  double watchdog_timeout_sec_{0.3};
  Transform tf_robot_camera_;
  Transform tf_arm1_robot_;
  Transform tf_arm2_robot_;
  bool v2_seq_init_{false};
  bool legacy_seq_init_{false};
  bool v2_seen_{false};
  uint32_t last_v2_seq_{0};
  uint32_t last_legacy_seq_{0};
  std::chrono::steady_clock::time_point last_valid_;
  rclcpp::Publisher<techx_vision_bridge::msg::VisionFrame>::SharedPtr frame_pub_;
  rclcpp::Publisher<techx_vision_bridge::msg::VisionObject>::SharedPtr object_pub_;
  rclcpp::Publisher<techx_vision_bridge::msg::VisionTarget>::SharedPtr detail_pub_;
  rclcpp::Publisher<techx_vision_bridge::msg::Target3D>::SharedPtr legacy_pub_;
  rclcpp::TimerBase::SharedPtr recv_timer_;
  rclcpp::TimerBase::SharedPtr watchdog_timer_;
};

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<VisionFrameBridgeNode>());
  rclcpp::shutdown();
  return 0;
}
