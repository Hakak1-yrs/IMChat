#include "Packet.h"

std::string Packet::encode() const {
    // 字符串里万一有不合法的 UTF-8，用替换字符代替，而不是抛异常
    return Frame::encode(type, body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
}

std::optional<Packet> Packet::fromFrame(const Frame& frame) {
    Packet packet;
    packet.type = frame.type;
    if (frame.body.empty()) return packet;
    packet.body = nlohmann::json::parse(frame.body, nullptr, false);   // 不抛异常，失败时得到 discarded
    if (packet.body.is_discarded() || !packet.body.is_object()) return std::nullopt;
    return packet;
}
