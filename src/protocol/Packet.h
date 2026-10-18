#pragma once
#include <optional>
#include <string>
#include <nlohmann/json.hpp>
#include "Frame.h"

// 一条协议消息：类型 + JSON 正文。Frame 只负责切分字节流，Packet 负责正文的序列化
struct Packet {
    MessageType type = MessageType::PING;
    nlohmann::json body = nlohmann::json::object();

    std::string encode() const;   // → 完整的一帧
    // 正文必须是 JSON 对象（空正文视为 {}）；不合法时返回空
    static std::optional<Packet> fromFrame(const Frame& frame);
};
