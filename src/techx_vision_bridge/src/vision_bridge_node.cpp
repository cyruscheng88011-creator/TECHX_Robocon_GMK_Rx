/**
 * @file vision_bridge_node.cpp
 * @brief UDP to ROS 2 bridge for TECHX vision data.
 *
 * Supported UDP protocols:
 *   V1 legacy 0x55AA: 29-byte single XYZ packet.
 *   V2 telemetry 0x55AB: per-inference frame status with 0..16 KFS targets.
 *
 * Competition behavior:
 *   - V2 count=0 is a valid online/no-target status frame.
 *   - V2 target z=0 carries recognition data but no valid 3D coordinate.
 *   - The old Target3D topic is still published only for valid XYZ targets.
 *   - The new VisionTarget topic carries status, class, color, confidence, pixel and XYZ.
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

#include "rclcpp/rclcpp.hpp"
#include "techx_vision_bridge/msg/target3_d.hpp"
#include "techx_vision_bridge/msg/vision_target.hpp"

constexpr uint16_t MAGIC_LEGACY = 0x55AA;
constexpr uint16_t MAGIC_V2 = 0x55AB;
constexpr uint8_t VERSION_V2 = 2;
constexpr size_t LEGACY_PACKET_SIZE = 29;
constexpr size_t LEGACY_CRC_COVERAGE = 27;
constexpr size_t RECV_BUF_SIZE = 4096;
constexpr size_t MAX_V2_TARGETS = 16;
constexpr size_t QUEUE_MAX_SIZE = 4;

#pragma pack(push, 1)
struct UdpLegacyPacket {
    uint16_t magic;
    uint32_t seq;
    double timestamp;
    uint8_t track_id;
    float x;
    float y;
    float z;
    uint16_t crc16;
};

struct UdpV2Header {
    uint16_t magic;
    uint8_t version;
    uint8_t flags;
    uint32_t seq;
    double timestamp;
    uint8_t count;
};

struct UdpV2Target {
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

static_assert(sizeof(UdpLegacyPacket) == 29, "legacy packet must be 29 bytes");
static_assert(sizeof(UdpV2Header) == 17, "V2 header must be 17 bytes");
static_assert(sizeof(UdpV2Target) == 27, "V2 target must be 27 bytes");

struct TargetData {
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
};

struct FrameData {
    uint8_t protocol_version{0};
    uint32_t seq{0};
    double timestamp{0.0};
    uint8_t target_count{0};
    std::vector<TargetData> targets;
    std::chrono::system_clock::time_point recv_time;
};

constexpr std::array<uint16_t, 256> buildCrc16Table() {
    std::array<uint16_t, 256> table{};
    for (int i = 0; i < 256; ++i) {
        uint16_t crc = static_cast<uint16_t>(i << 8);
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000) {
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
            } else {
                crc = static_cast<uint16_t>(crc << 1);
            }
        }
        table[i] = crc;
    }
    return table;
}

static constexpr auto CRC16_TABLE = buildCrc16Table();

inline uint16_t crc16Ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc = static_cast<uint16_t>(((crc << 8) & 0xFFFF) ^ CRC16_TABLE[((crc >> 8) ^ data[i]) & 0xFF]);
    }
    return crc;
}

inline bool finite3(float x, float y, float z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

class VisionBridgeNode : public rclcpp::Node {
public:
    VisionBridgeNode();
    ~VisionBridgeNode() override;

private:
    void recvLoop();
    void processLoop();
    void watchdogCallback();

    bool decodePacket(const uint8_t* data, size_t len, FrameData& out);
    bool decodeLegacy(const uint8_t* data, size_t len, FrameData& out);
    bool decodeV2(const uint8_t* data, size_t len, FrameData& out);
    bool acceptSeq(uint32_t seq);
    bool preferFrame(const FrameData& candidate, const FrameData& current, bool has_current) const;
    rclcpp::Time buildRosTime(const FrameData& frame);
    void publishFrame(const FrameData& frame);
    void publishLegacyTarget(const FrameData& frame, const TargetData& target);
    void publishVisionTarget(const FrameData& frame, const TargetData* target);
    void tryReconnect();
    void closeSocket();
    void applyThreadPriority();

    std::string _udp_bind_addr;
    int _udp_port{12345};
    std::string _topic_name;
    std::string _detail_topic_name;
    double _reconnect_timeout_sec{3.0};
    double _watchdog_timeout_sec{0.3};
    int _thread_nice{10};
    std::string _timestamp_mode;
    double _min_valid_ts{1500000000.0};
    double _max_future_sec{3600.0};
    bool _publish_legacy_topic{true};
    bool _accept_legacy{true};

    int _sock{-1};
    std::thread _recv_thread;
    std::thread _process_thread;
    std::atomic<bool> _stop{false};

    std::queue<FrameData> _queue;
    std::mutex _queue_mutex;
    std::condition_variable _queue_cv;

    std::mutex _state_mutex;
    uint32_t _last_seq{0};
    bool _seq_initialized{false};
    bool _v2_seen{false};
    std::chrono::steady_clock::time_point _last_valid_time;

    std::atomic<bool> _warned_mono_clock{false};
    std::atomic<bool> _warned_future_ts{false};

    rclcpp::Publisher<techx_vision_bridge::msg::Target3D>::SharedPtr _legacy_pub;
    rclcpp::Publisher<techx_vision_bridge::msg::VisionTarget>::SharedPtr _detail_pub;
    rclcpp::TimerBase::SharedPtr _watchdog_timer;
};

VisionBridgeNode::VisionBridgeNode() : Node("vision_bridge_node") {
    this->declare_parameter("udp_bind_addr", "0.0.0.0");
    this->declare_parameter("udp_port", 12345);
    this->declare_parameter("topic_name", "/techx/vision/targets");
    this->declare_parameter("detail_topic_name", "/techx/vision/kfs_targets");
    this->declare_parameter("reconnect_timeout_sec", 3.0);
    this->declare_parameter("watchdog_timeout_sec", 0.3);
    this->declare_parameter("thread_nice", 10);
    this->declare_parameter("timestamp_mode", "local");
    this->declare_parameter("min_valid_timestamp", 1500000000.0);
    this->declare_parameter("max_future_sec", 3600.0);
    this->declare_parameter("publish_legacy_topic", true);
    this->declare_parameter("accept_legacy", true);

    _udp_bind_addr = this->get_parameter("udp_bind_addr").as_string();
    _udp_port = this->get_parameter("udp_port").as_int();
    _topic_name = this->get_parameter("topic_name").as_string();
    _detail_topic_name = this->get_parameter("detail_topic_name").as_string();
    _reconnect_timeout_sec = this->get_parameter("reconnect_timeout_sec").as_double();
    _watchdog_timeout_sec = this->get_parameter("watchdog_timeout_sec").as_double();
    _thread_nice = this->get_parameter("thread_nice").as_int();
    _timestamp_mode = this->get_parameter("timestamp_mode").as_string();
    _min_valid_ts = this->get_parameter("min_valid_timestamp").as_double();
    _max_future_sec = this->get_parameter("max_future_sec").as_double();
    _publish_legacy_topic = this->get_parameter("publish_legacy_topic").as_bool();
    _accept_legacy = this->get_parameter("accept_legacy").as_bool();

    if (_timestamp_mode != "upstream" && _timestamp_mode != "local" && _timestamp_mode != "auto") {
        RCLCPP_WARN(this->get_logger(), "Invalid timestamp_mode '%s', fallback to local", _timestamp_mode.c_str());
        _timestamp_mode = "local";
    }

    RCLCPP_INFO(this->get_logger(),
        "params: udp=%s:%d legacy_topic=%s detail_topic=%s watchdog=%.2fs ts_mode=%s",
        _udp_bind_addr.c_str(), _udp_port, _topic_name.c_str(), _detail_topic_name.c_str(),
        _watchdog_timeout_sec, _timestamp_mode.c_str());

    auto qos = rclcpp::SensorDataQoS();
    qos.reliability(RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT);
    _legacy_pub = this->create_publisher<techx_vision_bridge::msg::Target3D>(_topic_name, qos);
    _detail_pub = this->create_publisher<techx_vision_bridge::msg::VisionTarget>(_detail_topic_name, qos);

    _last_valid_time = std::chrono::steady_clock::now();
    tryReconnect();

    _watchdog_timer = this->create_wall_timer(
        std::chrono::milliseconds(500),
        std::bind(&VisionBridgeNode::watchdogCallback, this)
    );

    _recv_thread = std::thread(&VisionBridgeNode::recvLoop, this);
    _process_thread = std::thread(&VisionBridgeNode::processLoop, this);

    RCLCPP_INFO(this->get_logger(), "VisionBridgeNode started");
}

VisionBridgeNode::~VisionBridgeNode() {
    _stop.store(true);
    _queue_cv.notify_all();
    closeSocket();
    if (_recv_thread.joinable()) {
        _recv_thread.join();
    }
    if (_process_thread.joinable()) {
        _process_thread.join();
    }
    RCLCPP_INFO(this->get_logger(), "VisionBridgeNode stopped");
}

void VisionBridgeNode::recvLoop() {
    pthread_setname_np(pthread_self(), "vbridge_recv");
    applyThreadPriority();
    uint8_t recv_buf[RECV_BUF_SIZE];
    RCLCPP_INFO(this->get_logger(), "UDP recv thread started");

    while (!_stop.load()) {
        if (_sock < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            tryReconnect();
            continue;
        }

        struct sockaddr_in src_addr{};
        socklen_t addr_len = sizeof(src_addr);
        ssize_t n = recvfrom(_sock, recv_buf, RECV_BUF_SIZE, 0,
                             reinterpret_cast<struct sockaddr*>(&src_addr), &addr_len);

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                auto now = std::chrono::steady_clock::now();
                double elapsed = 0.0;
                bool initialized = false;
                {
                    std::lock_guard<std::mutex> lock(_state_mutex);
                    elapsed = std::chrono::duration<double>(now - _last_valid_time).count();
                    initialized = _seq_initialized;
                }
                if (elapsed > _reconnect_timeout_sec && initialized) {
                    RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                        "UDP timeout %.2fs, reconnecting socket", elapsed);
                    tryReconnect();
                }
                continue;
            }
            if (_stop.load()) {
                break;
            }
            RCLCPP_ERROR_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "recvfrom error: %s", std::strerror(errno));
            continue;
        }

        FrameData best;
        bool has_best = false;
        if (decodePacket(recv_buf, static_cast<size_t>(n), best)) {
            has_best = true;
        }

        int flags = fcntl(_sock, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(_sock, F_SETFL, flags | O_NONBLOCK);
            uint8_t drain_buf[RECV_BUF_SIZE];
            while (true) {
                ssize_t dn = recvfrom(_sock, drain_buf, RECV_BUF_SIZE, 0, nullptr, nullptr);
                if (dn < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    break;
                }
                FrameData candidate;
                if (decodePacket(drain_buf, static_cast<size_t>(dn), candidate)) {
                    if (preferFrame(candidate, best, has_best)) {
                        best = candidate;
                        has_best = true;
                    }
                }
            }
            fcntl(_sock, F_SETFL, flags & ~O_NONBLOCK);
        }

        if (has_best) {
            {
                std::lock_guard<std::mutex> lock(_queue_mutex);
                while (_queue.size() >= QUEUE_MAX_SIZE) {
                    _queue.pop();
                }
                _queue.push(best);
            }
            _queue_cv.notify_one();
            {
                std::lock_guard<std::mutex> lock(_state_mutex);
                _last_valid_time = std::chrono::steady_clock::now();
            }
        }
    }

    RCLCPP_INFO(this->get_logger(), "UDP recv thread stopped");
}

bool VisionBridgeNode::preferFrame(const FrameData& candidate, const FrameData& current, bool has_current) const {
    if (!has_current) {
        return true;
    }
    if (candidate.protocol_version == 2 && current.protocol_version != 2) {
        return true;
    }
    if (candidate.protocol_version != 2 && current.protocol_version == 2) {
        return false;
    }
    return static_cast<int32_t>(candidate.seq - current.seq) > 0;
}

bool VisionBridgeNode::acceptSeq(uint32_t seq) {
    std::lock_guard<std::mutex> lock(_state_mutex);
    if (!_seq_initialized) {
        _last_seq = seq;
        _seq_initialized = true;
        return true;
    }
    if (seq == _last_seq) {
        return false;
    }
    if (static_cast<int32_t>(seq - _last_seq) <= 0) {
        return false;
    }
    _last_seq = seq;
    return true;
}

bool VisionBridgeNode::decodePacket(const uint8_t* data, size_t len, FrameData& out) {
    if (len < sizeof(uint16_t)) {
        return false;
    }
    uint16_t magic = 0;
    std::memcpy(&magic, data, sizeof(magic));
    if (magic == MAGIC_V2) {
        return decodeV2(data, len, out);
    }
    if (magic == MAGIC_LEGACY) {
        return decodeLegacy(data, len, out);
    }
    RCLCPP_DEBUG(this->get_logger(), "drop UDP packet with unknown magic 0x%04X len=%zu", magic, len);
    return false;
}

bool VisionBridgeNode::decodeLegacy(const uint8_t* data, size_t len, FrameData& out) {
    if (!_accept_legacy) {
        return false;
    }
    if (len != LEGACY_PACKET_SIZE) {
        RCLCPP_DEBUG(this->get_logger(), "legacy length mismatch: %zu", len);
        return false;
    }

    UdpLegacyPacket packet{};
    std::memcpy(&packet, data, sizeof(packet));
    uint16_t crc = crc16Ccitt(data, LEGACY_CRC_COVERAGE);
    if (crc != packet.crc16) {
        RCLCPP_DEBUG(this->get_logger(), "legacy CRC mismatch");
        return false;
    }
    if (!acceptSeq(packet.seq)) {
        return false;
    }

    out = FrameData{};
    out.protocol_version = 1;
    out.seq = packet.seq;
    out.timestamp = packet.timestamp;
    out.target_count = 1;
    out.recv_time = std::chrono::system_clock::now();

    TargetData t{};
    t.track_id = packet.track_id;
    t.class_id = 255;
    t.color = 0;
    t.confidence = 1.0f;
    t.x = packet.x;
    t.y = packet.y;
    t.z = packet.z;
    t.valid_xyz = packet.z > 0.0f && finite3(packet.x, packet.y, packet.z);
    out.targets.push_back(t);
    return true;
}

bool VisionBridgeNode::decodeV2(const uint8_t* data, size_t len, FrameData& out) {
    if (len < sizeof(UdpV2Header) + sizeof(uint16_t)) {
        return false;
    }

    UdpV2Header header{};
    std::memcpy(&header, data, sizeof(header));
    if (header.version != VERSION_V2) {
        RCLCPP_DEBUG(this->get_logger(), "unsupported V2 version %u", header.version);
        return false;
    }
    if (header.count > MAX_V2_TARGETS) {
        RCLCPP_DEBUG(this->get_logger(), "V2 target count too large: %u", header.count);
        return false;
    }

    const size_t expected = sizeof(UdpV2Header) + static_cast<size_t>(header.count) * sizeof(UdpV2Target) + sizeof(uint16_t);
    if (len != expected) {
        RCLCPP_DEBUG(this->get_logger(), "V2 length mismatch: %zu != %zu", len, expected);
        return false;
    }

    uint16_t received_crc = 0;
    std::memcpy(&received_crc, data + len - sizeof(uint16_t), sizeof(received_crc));
    uint16_t computed_crc = crc16Ccitt(data, len - sizeof(uint16_t));
    if (received_crc != computed_crc) {
        RCLCPP_DEBUG(this->get_logger(), "V2 CRC mismatch");
        return false;
    }
    if (!acceptSeq(header.seq)) {
        return false;
    }

    out = FrameData{};
    out.protocol_version = 2;
    out.seq = header.seq;
    out.timestamp = header.timestamp;
    out.target_count = header.count;
    out.recv_time = std::chrono::system_clock::now();
    out.targets.reserve(header.count);

    size_t offset = sizeof(UdpV2Header);
    for (uint8_t i = 0; i < header.count; ++i) {
        UdpV2Target raw{};
        std::memcpy(&raw, data + offset, sizeof(raw));
        offset += sizeof(raw);

        TargetData t{};
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
        out.targets.push_back(t);
    }

    {
        std::lock_guard<std::mutex> lock(_state_mutex);
        _v2_seen = true;
    }
    return true;
}

void VisionBridgeNode::processLoop() {
    pthread_setname_np(pthread_self(), "vbridge_proc");
    applyThreadPriority();
    RCLCPP_INFO(this->get_logger(), "process thread started");

    while (!_stop.load()) {
        FrameData frame;
        bool has_data = false;
        {
            std::unique_lock<std::mutex> lock(_queue_mutex);
            _queue_cv.wait(lock, [this] { return !_queue.empty() || _stop.load(); });
            if (_stop.load() && _queue.empty()) {
                break;
            }
            if (!_queue.empty()) {
                frame = std::move(_queue.front());
                _queue.pop();
                has_data = true;
            }
        }
        if (has_data) {
            publishFrame(frame);
        }
    }

    RCLCPP_INFO(this->get_logger(), "process thread stopped");
}

void VisionBridgeNode::publishFrame(const FrameData& frame) {
    if (frame.protocol_version == 2 && frame.target_count == 0) {
        publishVisionTarget(frame, nullptr);
        RCLCPP_DEBUG(this->get_logger(), "publish V2 no-target seq=%u", frame.seq);
        return;
    }

    for (const auto& target : frame.targets) {
        publishVisionTarget(frame, &target);
        if (_publish_legacy_topic && target.valid_xyz) {
            publishLegacyTarget(frame, target);
        }
    }
}

void VisionBridgeNode::publishLegacyTarget(const FrameData& frame, const TargetData& target) {
    techx_vision_bridge::msg::Target3D msg;
    msg.header.frame_id = "camera_link_" + std::to_string(target.track_id);
    msg.header.stamp = buildRosTime(frame);
    msg.track_id = target.track_id;
    msg.x = target.x;
    msg.y = target.y;
    msg.z = target.z;
    _legacy_pub->publish(msg);
}

void VisionBridgeNode::publishVisionTarget(const FrameData& frame, const TargetData* target) {
    techx_vision_bridge::msg::VisionTarget msg;
    msg.header.frame_id = target ? ("camera_link_" + std::to_string(target->track_id)) : "camera_link";
    msg.header.stamp = buildRosTime(frame);
    msg.seq = frame.seq;
    msg.protocol_version = frame.protocol_version;
    msg.target_count = frame.target_count;
    msg.has_target = (target != nullptr);
    msg.valid_xyz = (target != nullptr) && target->valid_xyz;

    if (target) {
        msg.track_id = target->track_id;
        msg.class_id = target->class_id;
        msg.color = target->color;
        msg.confidence = target->confidence;
        msg.u = target->u;
        msg.v = target->v;
        msg.x = target->valid_xyz ? target->x : 0.0f;
        msg.y = target->valid_xyz ? target->y : 0.0f;
        msg.z = target->valid_xyz ? target->z : 0.0f;
    }

    _detail_pub->publish(msg);
}

rclcpp::Time VisionBridgeNode::buildRosTime(const FrameData& frame) {
    if (_timestamp_mode == "local") {
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(frame.recv_time.time_since_epoch()).count();
        return rclcpp::Time(ns);
    }

    auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    double now_sec = static_cast<double>(now_ns) / 1e9;

    if (frame.timestamp < 0.0) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 10000,
            "negative upstream timestamp %.3f, using local time", frame.timestamp);
        return rclcpp::Time(now_ns);
    }

    if (_timestamp_mode == "auto") {
        if (frame.timestamp < _min_valid_ts) {
            bool expected = false;
            if (_warned_mono_clock.compare_exchange_strong(expected, true)) {
                RCLCPP_WARN(this->get_logger(), "upstream timestamp looks non-unix, fallback to local time");
            }
            return rclcpp::Time(now_ns);
        }
        if (frame.timestamp > now_sec + _max_future_sec) {
            bool expected = false;
            if (_warned_future_ts.compare_exchange_strong(expected, true)) {
                RCLCPP_WARN(this->get_logger(), "upstream timestamp is too far in future, fallback to local time");
            }
            return rclcpp::Time(now_ns);
        }
    }

    double ts = frame.timestamp;
    int64_t sec = static_cast<int64_t>(ts);
    double frac = ts - static_cast<double>(sec);
    int64_t nanosec = static_cast<int64_t>(frac * 1e9);
    if (nanosec < 0) {
        nanosec += 1000000000LL;
        sec -= 1;
    }
    if (nanosec >= 1000000000LL) {
        nanosec -= 1000000000LL;
        sec += 1;
    }
    return rclcpp::Time(sec, static_cast<uint32_t>(nanosec));
}

void VisionBridgeNode::watchdogCallback() {
    auto now = std::chrono::steady_clock::now();
    double elapsed = 0.0;
    {
        std::lock_guard<std::mutex> lock(_state_mutex);
        elapsed = std::chrono::duration<double>(now - _last_valid_time).count();
    }
    if (elapsed > _watchdog_timeout_sec) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
            "vision UDP signal lost for %.2fs", elapsed);
    }
}

void VisionBridgeNode::tryReconnect() {
    closeSocket();
    _sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (_sock < 0) {
        RCLCPP_ERROR(this->get_logger(), "socket create failed: %s", std::strerror(errno));
        return;
    }

    int opt = 1;
    setsockopt(_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct timeval tv{};
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int rcvbuf = 256 * 1024;
    setsockopt(_sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    int sk_prio = 6;
    setsockopt(_sock, SOL_SOCKET, SO_PRIORITY, &sk_prio, sizeof(sk_prio));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(_udp_port));
    addr.sin_addr.s_addr = inet_addr(_udp_bind_addr.c_str());

    if (bind(_sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        RCLCPP_ERROR(this->get_logger(), "bind %s:%d failed: %s",
            _udp_bind_addr.c_str(), _udp_port, std::strerror(errno));
        closeSocket();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(_state_mutex);
        _seq_initialized = false;
        _last_seq = 0;
        _last_valid_time = std::chrono::steady_clock::now();
    }
    _warned_mono_clock.store(false);
    _warned_future_ts.store(false);

    RCLCPP_INFO(this->get_logger(), "UDP bound to %s:%d", _udp_bind_addr.c_str(), _udp_port);
}

void VisionBridgeNode::closeSocket() {
    if (_sock >= 0) {
        shutdown(_sock, SHUT_RDWR);
        close(_sock);
        _sock = -1;
    }
}

void VisionBridgeNode::applyThreadPriority() {
    if (_thread_nice > 0) {
        int ret = setpriority(PRIO_PROCESS, 0, _thread_nice);
        if (ret != 0) {
            RCLCPP_WARN(this->get_logger(), "set nice=%d failed: %s", _thread_nice, std::strerror(errno));
        }
    }
}

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<VisionBridgeNode>();
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.spin();
    rclcpp::shutdown();
    return 0;
}
