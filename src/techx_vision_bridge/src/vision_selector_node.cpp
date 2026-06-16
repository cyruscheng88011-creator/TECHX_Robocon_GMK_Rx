#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "techx_vision_bridge/msg/vision_frame.hpp"
#include "techx_vision_bridge/msg/vision_object.hpp"
#include "techx_vision_bridge/msg/vision_request.hpp"
#include "techx_vision_bridge/msg/vision_selection.hpp"

namespace {
constexpr uint8_t INVALID_INDEX = 255;

float finite_or_zero(float v) {
  return std::isfinite(v) ? v : 0.0f;
}
}  // namespace

class VisionSelectorNode : public rclcpp::Node {
 public:
  VisionSelectorNode() : Node("vision_selector_node") {
    declare_parameter("frame_topic_name", "/techx/vision/frame");
    declare_parameter("request_topic_name", "/techx/vision/request");
    declare_parameter("selected_topic_name", "/techx/vision/selected");
    declare_parameter("reliable_qos", true);
    declare_parameter("qos_depth", 5);
    declare_parameter("request_timeout_sec", 0.0);
    declare_parameter("default_max_frame_age_sec", 0.20);

    request_timeout_sec_ = get_parameter("request_timeout_sec").as_double();
    default_max_frame_age_sec_ = get_parameter("default_max_frame_age_sec").as_double();

    const bool reliable = get_parameter("reliable_qos").as_bool();
    const int depth = std::max(1, static_cast<int>(get_parameter("qos_depth").as_int()));
    auto qos = rclcpp::QoS(rclcpp::KeepLast(depth));
    if (reliable) {
      qos.reliable();
    } else {
      qos.best_effort();
    }
    qos.durability_volatile();

    selected_pub_ = create_publisher<techx_vision_bridge::msg::VisionSelection>(
        get_parameter("selected_topic_name").as_string(), qos);
    frame_sub_ = create_subscription<techx_vision_bridge::msg::VisionFrame>(
        get_parameter("frame_topic_name").as_string(), qos,
        std::bind(&VisionSelectorNode::on_frame, this, std::placeholders::_1));
    request_sub_ = create_subscription<techx_vision_bridge::msg::VisionRequest>(
        get_parameter("request_topic_name").as_string(), qos,
        std::bind(&VisionSelectorNode::on_request, this, std::placeholders::_1));

    RCLCPP_INFO(get_logger(), "vision selector ready frame=%s request=%s selected=%s qos=%s",
                get_parameter("frame_topic_name").as_string().c_str(),
                get_parameter("request_topic_name").as_string().c_str(),
                get_parameter("selected_topic_name").as_string().c_str(),
                reliable ? "reliable" : "best_effort");
  }

 private:
  void on_frame(const techx_vision_bridge::msg::VisionFrame::SharedPtr msg) {
    latest_frame_ = *msg;
    frame_recv_time_ = std::chrono::steady_clock::now();
    has_frame_ = true;
    publish_selection();
  }

  void on_request(const techx_vision_bridge::msg::VisionRequest::SharedPtr msg) {
    latest_request_ = *msg;
    request_recv_time_ = std::chrono::steady_clock::now();
    has_request_ = true;
    publish_selection();
  }

  bool request_expired() const {
    if (!has_request_ || request_timeout_sec_ <= 0.0) return false;
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - request_recv_time_).count() > request_timeout_sec_;
  }

  float frame_age_sec() const {
    if (!has_frame_) return 0.0f;
    return static_cast<float>(std::chrono::duration<double>(std::chrono::steady_clock::now() - frame_recv_time_).count());
  }

  bool matches_request(const techx_vision_bridge::msg::VisionObject &obj) const {
    if (!has_request_) return false;
    if (latest_request_.target_type != 0 && obj.target_type != latest_request_.target_type) return false;
    if (latest_request_.zone_id != 0 && obj.zone_id != latest_request_.zone_id) return false;
    if (latest_request_.use_class_id && obj.class_id != latest_request_.class_id) return false;
    if (latest_request_.use_color && obj.color != latest_request_.color) return false;
    if (latest_request_.require_control_xyz && !obj.valid_control_xyz) return false;
    if (latest_request_.min_confidence > 0.0f && obj.confidence < latest_request_.min_confidence) return false;
    return true;
  }

  bool select_best(size_t &best_index, float &best_score) const {
    best_index = 0;
    best_score = -std::numeric_limits<float>::infinity();
    bool found = false;
    for (size_t i = 0; i < latest_frame_.targets.size(); ++i) {
      const auto &obj = latest_frame_.targets[i];
      if (!matches_request(obj)) continue;
      float score = finite_or_zero(obj.priority);
      if (obj.valid_control_xyz) score += 0.02f;
      if (score > best_score) {
        best_index = i;
        best_score = score;
        found = true;
      }
    }
    return found;
  }

  techx_vision_bridge::msg::VisionSelection base_msg(uint8_t status) const {
    techx_vision_bridge::msg::VisionSelection out;
    out.header.stamp = now();
    out.header.frame_id = "camera_link";
    out.frame_seq = has_frame_ ? latest_frame_.seq : 0;
    out.request_seq = has_request_ ? latest_request_.request_seq : 0;
    out.has_request = has_request_;
    out.has_match = false;
    out.status = status;
    out.selected_index = INVALID_INDEX;
    out.frame_age_sec = frame_age_sec();
    out.score = 0.0f;
    return out;
  }

  void publish_selection() {
    if (!has_request_) {
      return;
    }
    if (request_expired()) {
      selected_pub_->publish(base_msg(techx_vision_bridge::msg::VisionSelection::STATUS_REQUEST_STALE));
      has_request_ = false;
      return;
    }
    if (!has_frame_) {
      selected_pub_->publish(base_msg(techx_vision_bridge::msg::VisionSelection::STATUS_NO_FRAME));
      return;
    }

    const float max_age = latest_request_.max_frame_age_sec > 0.0f ? latest_request_.max_frame_age_sec : static_cast<float>(default_max_frame_age_sec_);
    if (max_age > 0.0f && frame_age_sec() > max_age) {
      selected_pub_->publish(base_msg(techx_vision_bridge::msg::VisionSelection::STATUS_FRAME_STALE));
      return;
    }

    size_t best_index = 0;
    float best_score = 0.0f;
    if (!select_best(best_index, best_score)) {
      selected_pub_->publish(base_msg(techx_vision_bridge::msg::VisionSelection::STATUS_NO_MATCH));
      return;
    }

    auto out = base_msg(techx_vision_bridge::msg::VisionSelection::STATUS_OK);
    out.has_match = true;
    out.selected_index = static_cast<uint8_t>(std::min<size_t>(best_index, INVALID_INDEX - 1));
    out.score = best_score;
    out.target = latest_frame_.targets[best_index];
    selected_pub_->publish(out);
  }

  bool has_frame_{false};
  bool has_request_{false};
  techx_vision_bridge::msg::VisionFrame latest_frame_;
  techx_vision_bridge::msg::VisionRequest latest_request_;
  std::chrono::steady_clock::time_point frame_recv_time_;
  std::chrono::steady_clock::time_point request_recv_time_;
  double request_timeout_sec_{0.0};
  double default_max_frame_age_sec_{0.20};

  rclcpp::Publisher<techx_vision_bridge::msg::VisionSelection>::SharedPtr selected_pub_;
  rclcpp::Subscription<techx_vision_bridge::msg::VisionFrame>::SharedPtr frame_sub_;
  rclcpp::Subscription<techx_vision_bridge::msg::VisionRequest>::SharedPtr request_sub_;
};

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<VisionSelectorNode>());
  rclcpp::shutdown();
  return 0;
}
